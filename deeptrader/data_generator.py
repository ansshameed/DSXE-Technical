#!/usr/bin/env python3
"""
Data Generator for DeepTrader

Purpose: Read the normalised data and generate data batches for training the DeepTrader model (LSTM network)

This script:
1. Reads normalised LOB snapshot data and counts number of data points in normalised dataset
2. Separates features from target variables
3. Loads min/max values from JSON file
3. Generates batches of data for training the LSTM model (first 13 columns are features, last column is target)
4. Format data properly for an LSTM model (samples, time steps, features); samples = individual LOB snapshots (datapoints), time steps = 1 (no temporal relationship between snapshots, each snapshot treated independently)
"""

import pickle
import numpy as np
from tensorflow.keras.utils import Sequence
import json
import os

# Constants
BATCHSIZE = 512  # (Change this back to 16384 once we have all the data). Number of LOB snapshots processed simulataneously through NN during each training step
NUMBER_OF_FEATURES = 13  # First 13 columns are features
NUMBER_OF_STEPS = 1      # For LSTM sequence length; treating each snapshot as independent datapoint without considering relationship to previous snapshots. If value e.g. 10 LSTM would be fed sequences of 10 snapshots to predict next value; learn temporal patterns in the market data for predictions 


class DeepTraderDataGenerator(Sequence):
    """Generates data batches for the DeepTrader model."""

    def __init__(self, dataset_path, batch_size=BATCHSIZE, n_features=NUMBER_OF_FEATURES):
        """Initialise the data generator."""
        self.dataset_path = dataset_path
        self.batch_size = batch_size
        self.n_features = n_features
        
        """ Count total items in the dataset (pickle file) by loading each batch (adding up length and initialising empty arrays to store normalisation values)""" 
        self.no_items = 0
        with open(self.dataset_path, "rb") as f:
            try:
                while True:
                    data_batch = pickle.load(f)
                    self.no_items += len(data_batch)
            except EOFError:
                pass  # End of file
        
        print(f"Total data points: {self.no_items}")
        
        # Initialise arrays for min/max values
        self.train_max = np.empty((self.n_features + 1))
        self.train_min = np.empty((self.n_features + 1))
        
        try:
            with open(os.path.join(os.path.dirname(dataset_path), 'min_max_values.json'), 'r') as f:
                min_max_data = json.load(f)
                self.train_min = np.array(min_max_data['min_values'])
                self.train_max = np.array(min_max_data['max_values'])
                print("Min: ", self.train_min)
                print("Max: ", self.train_max)
            print("Min/max values loaded successfully")
        except Exception as e:
            print(f"Warning: Could not load min/max values: {e}") # Keep the empty arrays as fallback

    def __len__(self):
        """Return the number of batches per epoch."""
        return self.no_items // self.batch_size

    def __getitem__(self, index):
        """Generate one batch of data."""
        # Calculate start and end indices for this batch
        start_idx = index * self.batch_size
        end_idx = min((index + 1) * self.batch_size, self.no_items)
        
        # Create empty arrays for features and targets
        x = np.empty((end_idx - start_idx, NUMBER_OF_STEPS, self.n_features)) # LSTM input shape (samples, time steps, features)
        y = np.empty((end_idx - start_idx, 1)) # LSTM output shape (samples, 1)
        
        # Read through the pickle file to get the right batch
        with open(self.dataset_path, "rb") as f:
            count = 0
            batch_count = 0
            
            try:
                while True:
                    data_batch = pickle.load(f)
                    batch_size = len(data_batch)
                    
                    # Skip batches that don't overlap with our target indices
                    if count + batch_size <= start_idx:
                        count += batch_size
                        continue
                    
                    # Process items that fall within our target range
                    for item_idx, item in enumerate(data_batch):
                        if start_idx <= count < end_idx:
                            # Split into features and target
                            features = item[:self.n_features]
                            target = item[self.n_features]
                            
                            # Reshape for LSTM input (samples, time steps, features)
                            x[batch_count] = features.reshape(NUMBER_OF_STEPS, self.n_features)
                            y[batch_count] = target
                            
                            batch_count += 1
                        
                        count += 1
                        if count >= end_idx:
                            break
                    
                    if count >= end_idx:
                        break
                        
            except EOFError:
                pass  # End of file
        
        return x, y


if __name__ == "__main__":
    # Example usage
    data_path = "normalised_data/normalised_data.pkl"
    generator = DeepTraderDataGenerator(data_path)
    
    # Test the first batch
    x_batch, y_batch = generator[0]
    print(f"Batch shape - X: {x_batch.shape}, y: {y_batch.shape}")
    print("Sample X (first row):") # 512 samples, 1 time step, 13 features
    print(x_batch[0]) # First row of batch data (13 features) 
    print("Sample y (first 5 values):") # 512 samples, 1 target value
    print(y_batch[:5]) # 5 rows of target data (one for each snapshot)

    """ 
    Currently: Uses batches of 512 samples (LOB snapshots) for training the LSTM model. Only one batch is generated in this example.
    Training the LSTM: Will pass entire generator to model.fit() function, which will call __getitem__() to get batches of data for training.
    That will process all the data points in batches of 512 samples until all data points have been processed.
    """