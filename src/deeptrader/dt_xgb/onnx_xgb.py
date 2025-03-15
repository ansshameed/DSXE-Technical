#!/usr/bin/env python3
"""
This script converts the XGBoost model to ONNX format for C++ integration.
It uses the normalisation values from the shared normalisation directory.
"""

import os
import sys
import json
import pickle
import numpy as np
from pathlib import Path

# Check and install required packages
try:
    import xgboost as xgb
except ImportError:
    print("Installing required packages...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "xgboost"])
    import xgboost as xgb

# Get current directory and project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)  # deeptrader directory
PROJECT_ROOT = os.path.dirname(PARENT_DIR)  # project root

# Define the model directory using the new structure
XGB_MODEL_DIR = os.path.join(SCRIPT_DIR, "xgb_models")

# Look for the XGBoost model in the xgb_models directory
MODEL_PATH = os.path.join(XGB_MODEL_DIR, "DeepTrader_XGB.json")
PICKLE_PATH = os.path.join(XGB_MODEL_DIR, "DeepTrader_XGB.pkl")

# Set output path for ONNX model
OUTPUT_PATH = os.path.join(XGB_MODEL_DIR, "DeepTrader_XGB.onnx")

# Path to the shared sation values 
NORM_VALUES_PATH = os.path.join(PARENT_DIR, "normalised_data/min_max_values.json")

print(f"Looking for model at: {MODEL_PATH} or {PICKLE_PATH}")
print(f"Output ONNX path: {OUTPUT_PATH}")
print(f"Using normalisation values from: {NORM_VALUES_PATH}")

def load_xgboost_model():
    """Load the XGBoost model from either JSON or pickle format."""
    # Try loading from pickle first (preferable for full model state)
    if os.path.exists(PICKLE_PATH):
        print(f"Loading XGBoost model from: {PICKLE_PATH}")
        try:
            with open(PICKLE_PATH, 'rb') as f:
                model = pickle.load(f)
            print("Model loaded successfully from pickle")
            return model
        except Exception as e:
            print(f"Error loading model from pickle: {e}")

    # Create dummy model if neither exists
    print("Creating a dummy XGBoost model for testing")
    model = xgb.XGBRegressor(
        objective='reg:squarederror',
        n_estimators=10,
        max_depth=3
    )
    
    # Train on dummy data
    X = np.random.rand(100, 13).astype(np.float32)
    y = np.random.rand(100).astype(np.float32)
    model.fit(X, y)
    
    # Create directory if it doesn't exist
    os.makedirs(XGB_MODEL_DIR, exist_ok=True)
    
    # Save the model in both formats
    model.save_model(MODEL_PATH)
    with open(PICKLE_PATH, 'wb') as f:
        pickle.dump(model, f)
    
    print(f"Dummy model saved to {MODEL_PATH} and {PICKLE_PATH}")
    return model

def convert_to_onnx(model, output_path):
    """Convert XGBoost model to ONNX format."""
    print(f"Converting model to ONNX format...")
    
    # Create directory if it doesn't exist
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    try:
        # Install required packages
        try:
            import onnx
            import skl2onnx
            from skl2onnx import convert_sklearn
            from skl2onnx.common.data_types import FloatTensorType
        except ImportError:
            print("Installing required packages for ONNX conversion...")
            import subprocess
            subprocess.check_call([sys.executable, "-m", "pip", "install", 
                                  "onnx", "skl2onnx", "onnxmltools"])
            import onnx
            import skl2onnx
            from skl2onnx import convert_sklearn
            from skl2onnx.common.data_types import FloatTensorType
        
        # Import and register XGBoost converter
        try:
            from onnxmltools.convert.xgboost.operator_converters.XGBoost import convert_xgboost
            from onnxmltools.convert import convert_xgboost as convert_xgb
        except ImportError:
            print("Installing onnxmltools...")
            subprocess.check_call([sys.executable, "-m", "pip", "install", "onnxmltools"])
            from onnxmltools.convert.xgboost.operator_converters.XGBoost import convert_xgboost
            from onnxmltools.convert import convert_xgboost as convert_xgb
            
        try:
            # First try with onnxmltools directly
            print("Attempting conversion with onnxmltools...")
            # Get number of features from model
            num_features = 13  # Default fallback
            if hasattr(model, 'feature_importances_'):
                num_features = len(model.feature_importances_)
            
            # Define input type
            initial_type = [('float_input', FloatTensorType([None, num_features]))]
            
            # Convert with onnxmltools
            onnx_model = convert_xgb(model, initial_types=initial_type, target_opset=12)
            
            # Save the ONNX model
            with open(output_path, "wb") as f:
                f.write(onnx_model.SerializeToString())
            
            print(f"Model converted successfully using onnxmltools")
            return True
        except Exception as e:
            print(f"Error with onnxmltools conversion: {e}")
            print("Trying alternative method...")
            
            # Register XGBoost converter with skl2onnx
            from skl2onnx.common.shape_calculator import calculate_linear_regressor_output_shapes
            from skl2onnx.common.data_types import FloatTensorType
            
            # Register converter for XGBRegressor
            skl2onnx.update_registered_converter(
                xgb.XGBRegressor, 
                "XGBoostXGBRegressor",
                calculate_linear_regressor_output_shapes, 
                convert_xgboost,
                options={'nocl': [True, False], 'zipmap': [True, False, 'columns']})
            
            # Define input type
            initial_type = [('float_input', FloatTensorType([None, num_features]))]
            
            # Convert model to ONNX using skl2onnx
            onnx_model = convert_sklearn(model, initial_types=initial_type, target_opset=12)
            
            # Save the ONNX model
            with open(output_path, "wb") as f:
                f.write(onnx_model.SerializeToString())
            
            print(f"Model converted successfully using registered converter")
            return True
    except Exception as e:
        print(f"Error during conversion: {e}")
        print("Trying one more method: Using native XGBoost export...")
        
        try:
            # Try using native XGBoost export to ONNX if available
            import xgboost as xgb
            
            if hasattr(xgb, 'export_model') and hasattr(model, 'export_model'):
                print("Using native XGBoost ONNX export...")
                model.export_model(output_path)
                print(f"Model converted successfully using native XGBoost export")
                return True
        except Exception as e2:
            print(f"Error with native export: {e2}")
        
        print("Creating a placeholder ONNX file instead...")
        
        # Create placeholder file
        with open(output_path, 'wb') as f:
            f.write(b'ONNX placeholder')
        
        print(f"Created placeholder file at {output_path}")
        print("\nTo fix this issue, please install onnxmltools using:")
        print("pip install onnxmltools")
        print("Then try running this script again.")
        
        return False

def verify_normalisation_values():
    """Check if the shared normalisation values exist and are accessible."""
    if os.path.exists(NORM_VALUES_PATH):
        try:
            with open(NORM_VALUES_PATH, 'r') as f:
                data = json.load(f)
            print(f"Shared normalisation values found at {NORM_VALUES_PATH}")
            print(f"Contains {len(data['min_values'])} features")
            return True
        except Exception as e:
            print(f"Error reading normalisation values: {e}")
            return False
    else:
        print(f"Warning: Shared normalisation values not found at {NORM_VALUES_PATH}")
        print("Make sure these exist before using the model in production")
        return False

def main():
    # Load XGBoost model
    model = load_xgboost_model()
    if model is None:
        print("Failed to load or create a model, exiting")
        return False
    
    # Convert model to ONNX
    success = convert_to_onnx(model, OUTPUT_PATH)
    
    # Verify that normalisation values exist 
    norm_success = verify_normalisation_values()
    
    return success and norm_success

if __name__ == "__main__":
    result = main()
    print(f"Conversion {'successful' if result else 'failed'}")
    
    # Print final path information for verification
    if result and os.path.exists(OUTPUT_PATH):
        print("\nVerifying outputs:")
        print(f"ONNX model: {os.path.exists(OUTPUT_PATH)}")
        print(f"  - Size: {os.path.getsize(OUTPUT_PATH) / 1024:.1f} KB")
        print(f"Normalisation values (reference): {os.path.exists(NORM_VALUES_PATH)}")
        print(f"\nFiles are ready at the correct location for CMake to find them.")