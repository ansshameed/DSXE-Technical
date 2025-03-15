#!/usr/bin/env python3
"""
LSTM Model for DeepTrader

This script:
1. Defines the LSTM neural network architecture
2. Loads normalised data using the data generator
3. Trains the model on the data
4. Saves the trained model and plots the training loss
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense
from tensorflow.keras.optimizers import Adam
from tensorflow.keras.losses import MeanSquaredError
from tensorflow.keras.metrics import MeanAbsoluteError, MeanSquaredLogarithmicError
from tensorflow.keras.callbacks import ReduceLROnPlateau, EarlyStopping

# Get the current directory and add parent to path to access shared modules
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)  # DeepTrader dir
PROJECT_ROOT = os.path.dirname(PARENT_DIR)  # Project root

# Model dir
LSTM_MODEL_DIR = os.path.join(SCRIPT_DIR, "lstm_models")

# Path to normalisation values (min/max values)
NORM_VALUES_PATH = os.path.join(PARENT_DIR, "normalised_data/min_max_values.json")

# Add parent dir to the path to import shared modules
sys.path.append(PARENT_DIR)

from data_generator import DeepTraderDataGenerator, BATCHSIZE, NUMBER_OF_FEATURES, NUMBER_OF_STEPS

class DeepTraderLSTM: 

    """LSTM model for DeepTrader"""

    def __init__(self, input_shape, model_name="DeepTrader_LSTM"):

        """Initialise the LSTM model."""
        self.model_name = model_name
        self.input_shape = input_shape # (batch_size, time_steps, features)

        # Create model 
        self.model = Sequential()

        # LSTM layer - expects input shape (batch_size, features)
        self.model.add(
            LSTM(
                10,
                activation="relu",
                input_shape=(input_shape[1], input_shape[2]),
                unroll=True,
            )
        )
        self.model.add(Dense(5, activation="relu"))
        self.model.add(Dense(3, activation="relu"))
        self.model.add(Dense(1))

        # Compile the model 
        optimiser = Adam(learning_rate=1.5e-5)
        self.model.compile( 
            optimizer=optimiser,
            loss=MeanSquaredError(),  # Use the class instead of string
            metrics=[
                MeanAbsoluteError(), 
                MeanSquaredLogarithmicError()
            ]
        )

        # Summary of the model 
        self.model.summary()

    def train(self, data_generator, epochs=20):

        """ Train the model using the data generator """ 

        #Train the model 
        history = self.model.fit(
            data_generator,
            epochs=epochs,
            verbose=1, 
        )

        # Save training history 
        self.history = history.history

        return self.history

    def save_model(self): 
        """ Save the trained model """
        # Create model directory
        os.makedirs(LSTM_MODEL_DIR, exist_ok=True)
        
        # Save the entire model, not just weights
        model_path = os.path.join(LSTM_MODEL_DIR, f"{self.model_name}.keras")
        self.model.save(model_path, save_format='keras')
        
        print(f"Model saved to {model_path}")
        
        # Verify that shared normalisation values exist
        self.verify_normalisation_values()
        
    def verify_normalisation_values(self):
        """Verify that shared normalisation values exist."""
        if os.path.exists(NORM_VALUES_PATH):
            try:
                import json
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

    def plot_training_history(self): 

        """ Plot the training loss and metrics """
        # Create a new figure with controlled size
        fig = plt.figure(figsize=(10, 6))

        # Plot the loss 
        ax1 = fig.add_subplot(2, 1, 1)
        ax1.plot(self.history["loss"], label="loss")
        ax1.set_xlabel("Epoch")
        ax1.set_ylabel("Loss (MSE)")
        ax1.set_title('Model Loss')

        # Plot metrics 
        if 'mean_absolute_error' in self.history:
            ax2 = fig.add_subplot(2, 1, 2)
            ax2.plot(self.history["mean_absolute_error"], label="MAE")
            if 'mean_squared_logarithmic_error' in self.history:
                ax2.plot(self.history['mean_squared_logarithmic_error'], label='MSLE')
            ax2.set_xlabel("Epoch")
            ax2.set_ylabel("Metrics")
            ax2.set_title('Model Metrics')
            ax2.legend()

        fig.tight_layout()

        # Save the plot to the model directory
        plots_dir = os.path.join(LSTM_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        
        fig.savefig(os.path.join(plots_dir, "training_history.png"))
        plt.close(fig)  # Close the figure to prevent it from showing
        
        saved_img = plt.imread(os.path.join(plots_dir, "training_history.png"))
        plt.figure(figsize=(10, 6))
        plt.imshow(saved_img)
        plt.axis('off')
        plt.show()

def main(): 
    # Define model input shape: (batch_size, time_steps, features)
    input_shape = (BATCHSIZE, NUMBER_OF_STEPS, NUMBER_OF_FEATURES)

    # Create data generator using the shared normalised data path
    data_path = os.path.join(PARENT_DIR, "normalised_data/normalised_data.pkl")
    data_generator = DeepTraderDataGenerator(data_path)
    
    # Print a sample of training data
    print("Printing a sample of the training data:")
    x_batch, y_batch = data_generator[0]  # Get the first batch
    
    # Print statistics about the target values
    print(f"Target value statistics:")
    print(f"Mean: {np.mean(y_batch)}")
    print(f"Std: {np.std(y_batch)}")
    print(f"Min: {np.min(y_batch)}")
    print(f"Max: {np.max(y_batch)}")
    
    # Print a few examples
    print("\nFeature examples (first 3 samples):")
    for i in range(min(3, x_batch.shape[0])):
        print(f"Sample {i+1}:")
        for step in range(min(3, NUMBER_OF_STEPS)):
            print(f"  Step {step+1}: {x_batch[i, step]}")
        print(f"  Target: {y_batch[i, 0]}")
    
    # Create and train the LSTM model 
    lstm_model = DeepTraderLSTM(input_shape)
    history = lstm_model.train(data_generator, epochs=20)

    # Save and visualise the results 
    lstm_model.save_model()
    lstm_model.plot_training_history()

    print("Model training complete.")

if __name__ == "__main__":
    main()