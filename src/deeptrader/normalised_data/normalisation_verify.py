#!/usr/bin/env python3

import pickle
import numpy as np

# Load the pickle file
with open('normalised_data.pkl', 'rb') as f:
    try:
        while True:
            data = pickle.load(f)
            # Print shape and sample
            print(f"Data shape: {data.shape}") # Should be 14 features
            print("Sample (first 5 rows):") # First 5 rows of batch data
            print(data[:5])
            print("\n")
    except EOFError:
        # End of file
        pass