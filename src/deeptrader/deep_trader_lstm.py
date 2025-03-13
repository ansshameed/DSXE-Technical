#!/usr/bin/env python3
"""
LSTM Model for DeepTrader

This script:
1. Defines the LSTM neural network architecture
2. Loads normalized data using the data generator
3. Trains the model on the data
4. Saves the trained model and plots the training loss
"""

import os
import numpy as np
import matplotlib.pyplot as plt
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import LSTM, Dense
from tensorflow.keras.optimizers import Adam
from tensorflow.keras.losses import MeanSquaredError
from tensorflow.keras.metrics import MeanAbsoluteError, MeanSquaredLogarithmicError
from tensorflow.keras.callbacks import ReduceLROnPlateau, EarlyStopping


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

        # Save training historu 
        self.history = history.history

        return self.history

    def save_model(self): 
        """ Save the trained model """
        model_dir = f"models/{self.model_name}"
        os.makedirs(model_dir, exist_ok=True)
        
        # Save the entire model, not just weights
        self.model.save(f"{model_dir}/{self.model_name}.keras", save_format='keras')
        print(f"Model saved to {model_dir}/")
        
        # Save the min/max values alongside model for later use (could copy from data_generator)

    def plot_training_history(self): 

        """ PLot the training loss and metrics """
        plt.figure(figsize=(10, 6))

        # Plot the loss 
        plt.subplot(2, 1, 1)
        plt.plot(self.history["loss"], label="loss")
        plt.xlabel("Epoch")
        plt.ylabel("Loss (MSE)")
        plt.title('Model Loss')

        # Plot metrics 
        if 'mae' in self.history:
            plt.subplot(2, 1, 2)
            plt.plot(self.history["mae"], label="MAE")
            if 'msle' in self.history:
                plt.plot(self.history['msle'], label='MSLE')
            plt.xlabel("Epoch")
            plt.ylabel("Metrics")
            plt.title('Model Metrics')
            plt.legend()

        plt.tight_layout()

        # Create directory (if it doesn't exist) and save the plot
        model_dir = f"models/{self.model_name}"
        os.makedirs(model_dir, exist_ok=True)

        # Save the plot 
        plt.savefig(f"{model_dir}/training_history.png")
        plt.show() 

def main(): 
    # Define model input shape: (batch_size, time_steps, features)
    input_shape = (BATCHSIZE, NUMBER_OF_STEPS, NUMBER_OF_FEATURES)

    # Create data generator
    data_path = "normalised_data/normalised_data.pkl"
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
    for i in range(3):
        print(f"Sample {i+1}:")
        for step in range(NUMBER_OF_STEPS):
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

