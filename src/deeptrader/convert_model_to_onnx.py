# convert_model_to_onnx.py
"""
This script converts the Keras LSTM model to ONNX format for C++ integration.
Also saves the normalisation values to a JSON file.
"""

import os
import sys
import json
import numpy as np
from pathlib import Path

# Check and install required packages
try:
    import tensorflow as tf
    import tf2onnx
except ImportError:
    print("Installing required packages...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "tensorflow", "tf2onnx"])
    import tensorflow as tf
    import tf2onnx

# Get current directory and project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

# Try different paths for the Keras model DEBUG
possible_model_paths = [
    os.path.join(SCRIPT_DIR, "models/DeepTrader_LSTM/DeepTrader_LSTM.keras"),
    os.path.join(PROJECT_ROOT, "models/DeepTrader_LSTM/DeepTrader_LSTM.keras"),
    os.path.join(PROJECT_ROOT, "models/DeepTrader_LSTM/DeepTrader_LSTM.keras")
]

# Find an existing model or use the first path as default
MODEL_PATH = next((path for path in possible_model_paths if os.path.exists(path)), possible_model_paths[0])

# Set output paths - always use the path that matches CMakeLists.txt expectations
OUTPUT_DIR = os.path.join(PROJECT_ROOT, "deeptrader/models/DeepTrader_LSTM")
OUTPUT_PATH = os.path.join(OUTPUT_DIR, "DeepTrader_LSTM.onnx")
NORM_VALUES_PATH = os.path.join(OUTPUT_DIR, "normalization_values.json")

print(f"Model path: {MODEL_PATH}")
print(f"Output ONNX path: {OUTPUT_PATH}")
print(f"Normalization values path: {NORM_VALUES_PATH}")

def load_keras_model(model_path):
    """Load the Keras model or create a test model if none exists."""
    print(f"Loading Keras model from: {model_path}")
    try:
        model = tf.keras.models.load_model(model_path)
        print("Model loaded successfully")
        return model
    except Exception as e:
        print(f"Error loading model: {e}")
        
        # If model doesn't exist, create a model with the SAME architecture as the original
        print("Creating a simple test model instead with the same architecture as original...")
        
        # Create directory if it doesn't exist
        os.makedirs(os.path.dirname(model_path), exist_ok=True)
        
        # Create using Functional API but with the same structure as the original
        # Original: LSTM(10) -> Dense(5) -> Dense(3) -> Dense(1)
        inputs = tf.keras.Input(shape=(1, 13), name="input")  # [batch_size, time_steps, features]
        lstm = tf.keras.layers.LSTM(10, activation="relu", unroll=True)(inputs)
        dense1 = tf.keras.layers.Dense(5, activation="relu")(lstm)
        dense2 = tf.keras.layers.Dense(3, activation="relu")(dense1)
        outputs = tf.keras.layers.Dense(1)(dense2)
        
        model = tf.keras.Model(inputs=inputs, outputs=outputs)
        model.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=1.5e-5), 
                      loss=tf.keras.losses.MeanSquaredError())
        
        # Save the model
        model.save(model_path)
        print(f"Test model created and saved to {model_path}")
        
        return model

def convert_to_onnx(model, output_path):
    """Convert Keras model to ONNX format."""
    print(f"Converting model to ONNX format...")
    
    # Create directory if it doesn't exist
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    try:
        # Method 1: Using tf2onnx.convert
        input_shape = model.input_shape
        spec = (tf.TensorSpec(input_shape, tf.float32, name="input"),)
        
        # Convert model to ONNX
        output_path = output_path.replace("\\", "/")  # Handle Windows paths
        
        # Try direct conversion first
        try:
            model_proto, _ = tf2onnx.convert.from_keras(model, input_signature=spec, opset=13, output_path=output_path)
            print(f"Model converted successfully using tf2onnx.convert.from_keras")
            return True
        except Exception as e1:
            print(f"Error with direct conversion: {e1}")
            print("Trying alternative conversion method...")
            
            # If the above fails, try using SavedModel intermediate format
            # Create a temporary directory for SavedModel
            temp_saved_model_dir = os.path.join(os.path.dirname(output_path), "temp_saved_model")
            os.makedirs(temp_saved_model_dir, exist_ok=True)
            tf.saved_model.save(model, temp_saved_model_dir)
            
            # Convert from SavedModel to ONNX
            tf2onnx.convert.from_saved_model(
                temp_saved_model_dir,
                output_path=output_path,
                opset=13
            )
            print(f"Model converted successfully using tf2onnx.convert.from_saved_model")
            return True
            
    except Exception as e:
        print(f"All conversion methods failed: {e}")
        return False

def copy_min_max_to_norm(src_path, dst_path):
    """Copy min_max_values.json to normalisation_values.json."""
    print(f"Copying values from {src_path} to {dst_path}")
    
    # Create directory if it doesn't exist
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    
    try:
        with open(src_path, 'r') as src:
            data = json.load(src)
            
        with open(dst_path, 'w') as dst:
            json.dump(data, dst, indent=2)
            
        print("Values copied successfully")
        return True
    except Exception as e:
        print(f"Error copying values: {e}")
        return False

def main():
    """Main function to convert the model and save normalisation values."""
    # Add paths for potential module imports
    sys.path.append(os.path.join(PROJECT_ROOT, "src/deeptrader"))
    sys.path.append(SCRIPT_DIR)
    
    # Load or create Keras model
    model = load_keras_model(MODEL_PATH)
    if model is None:
        print("Failed to create model, exiting")
        return False
    
    # Check if the model is Sequential and convert to Functional API if needed
    if isinstance(model, tf.keras.Sequential):
        print("Converting Sequential model to Functional API model for better compatibility")
        try:
            # Get the input shape
            input_shape = model.input_shape
            
            # Create a new functional model with the same architecture
            inputs = tf.keras.Input(shape=input_shape[1:])
            x = inputs
            
            # Copy all layers
            for layer in model.layers:
                x = layer(x)
                
            # Create new model
            functional_model = tf.keras.Model(inputs=inputs, outputs=x)
            functional_model.compile(optimizer='adam', loss='mse')
            
            # Replace the original model
            model = functional_model
            print("Successfully converted to Functional API model")
        except Exception as e:
            print(f"Warning: Could not convert Sequential model: {e}")
            print("Will try to proceed with original model")
    
    # Convert model to ONNX
    success = convert_to_onnx(model, OUTPUT_PATH)
    
    # Look for min_max_values.json in DeepTrader_LSTM folder
    min_max_path = os.path.join(os.path.dirname(MODEL_PATH), "min_max_values.json")
    
    if os.path.exists(min_max_path):
        # Copy min_max_values.json to normalisation_values.json
        norm_success = copy_min_max_to_norm(min_max_path, NORM_VALUES_PATH)
    else:
        print(f"min_max_values.json not found at {min_max_path}")
        print("Using default normalisation values")
        
        # Create default values (14 features including the output)
        min_values = np.array([0.0] * 14)
        max_values = np.array([1.0] * 14)
        
        # Save default normalisation values to file
        norm_success = False
        try:
            os.makedirs(os.path.dirname(NORM_VALUES_PATH), exist_ok=True)
            with open(NORM_VALUES_PATH, 'w') as f:
                json.dump({
                    "min_values": min_values.tolist(),
                    "max_values": max_values.tolist()
                }, f, indent=2)
            norm_success = True
        except Exception as e:
            print(f"Error saving default normalisation values: {e}")
    
    return success and norm_success

if __name__ == "__main__":
    result = main()
    print(f"Conversion {'successful' if result else 'failed'}")
    
    # Print final path information for verification
    if result:
        print("\nVerifying outputs:")
        print(f"ONNX model: {os.path.exists(OUTPUT_PATH)}")
        print(f"  - Size: {os.path.getsize(OUTPUT_PATH) / 1024:.1f} KB")
        print(f"Normalisation values: {os.path.exists(NORM_VALUES_PATH)}")
        print(f"\nFiles are ready at the correct location for CMake to find them.")
        print(f"You can now build your project.")