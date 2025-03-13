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
NUMBER_OF_STEPS = 1      # For LSTM sequence length; treating each snapshot as independent datapoint without considering relationship to previous snapshots. If value e.g. 10 LSTM would be fed sequences of 10 snapshots to predict next value; learn temporal patterns in the market data for predictions. 5 = 5 consecutive lob snapshots to learn temporal patterns


class DeepTraderDataGenerator(Sequence):
    """Generates data batches for the DeepTrader model."""

    def __init__(self, dataset_path, batch_size=BATCHSIZE, n_features=NUMBER_OF_FEATURES, n_steps=NUMBER_OF_STEPS):
        """Initialise the data generator."""
        self.dataset_path = dataset_path
        self.batch_size = batch_size
        self.n_features = n_features
        self.n_steps = n_steps
        
        """ Count total items in the dataset (pickle file) by loading each batch (adding up length and initialising empty arrays to store normalisation values)""" 
        self.no_items = 0
        self.data_batches = []

        with open(self.dataset_path, "rb") as f:
            try:
                while True:
                    data_batch = pickle.load(f)
                    self.no_items += len(data_batch)
                    self.data_batches.append(len(data_batch))
            except EOFError:
                pass  # End of file
        
        print(f"Total data points: {self.no_items}")

        # Adjust total items to account for sequence length. Each sequence needs n_steps previous snapshots
        self.seq_items = self.no_items - (self.n_steps - 1)
        if self.seq_items <= 0:
            raise ValueError(f"Not enough data points for sequence length {n_steps}")
        
        print(f"Total data points: {self.no_items}")
        print(f"Total sequences: {self.seq_items}")
        
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
        return self.seq_items // self.batch_size

    def __getitem__(self, index):
        """Generate one batch of data."""
        # Calculate start and end indices for this batch
        start_idx = index * self.batch_size
        end_idx = min((index + 1) * self.batch_size, self.seq_items)
        batch_size = end_idx - start_idx
        
        # Create empty arrays for features and targets
        x = np.empty((batch_size, self.n_steps, self.n_features))  # (batch, time_steps, features)
        y = np.empty((batch_size, 1))  # (batch, target)
        
        # Read all data into memory for sequence creation
        all_data = []
        with open(self.dataset_path, "rb") as f:
            try:
                while True:
                    data_batch = pickle.load(f)
                    all_data.extend(data_batch)
            except EOFError:
                pass
        
        # Create sequences for each batch item
        for i in range(batch_size):
            seq_start = start_idx + i
            
            # Get n_steps consecutive snapshots as input sequence
            for step in range(self.n_steps):
                x[i, step] = all_data[seq_start + step][:self.n_features]
            
            # Target is the value that comes AFTER the sequence
            if seq_start + self.n_steps < len(all_data):
                y[i, 0] = all_data[seq_start + self.n_steps][self.n_features]
            else:
                # If we're at the end of the dataset, use the last value
                y[i, 0] = all_data[seq_start + self.n_steps - 1][self.n_features]
        
        return x, y


if __name__ == "__main__":
    # Example usage
    data_path = "normalised_data/normalised_data.pkl"
    generator = DeepTraderDataGenerator(data_path)
    
    # Test the first batch
    x_batch, y_batch = generator[0]
    print(f"Batch shape - X: {x_batch.shape}, y: {y_batch.shape}")
    
    print("\nSequence for first sample:")
    for i in range(NUMBER_OF_STEPS):
        print(f"Timestep {i+1}: {x_batch[0, i]}")
    print(f"Target value: {y_batch[0, 0]}")
    
    print("\nFirst 5 target values:")
    print(y_batch[:5])