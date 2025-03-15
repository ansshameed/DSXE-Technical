#!/usr/bin/env python3
"""
XGBoost Model for DeepTrader

This script:
1. Reuses the existing normalised data from the LSTM pipeline
2. Trains an XGBoost model on the flat feature vectors
3. Saves the model and generates visualizations
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import json
import pickle
import xgboost as xgb
from sklearn.metrics import mean_squared_error, mean_absolute_error

# Get the current directory and add parent to path to access shared modules
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)  # Deep trader dir
PROJECT_ROOT = os.path.dirname(PARENT_DIR)  # Root dir

# Model dir
XGB_MODEL_DIR = os.path.join(SCRIPT_DIR, "xgb_models")

# Path to shared normalisation values
NORM_VALUES_PATH = os.path.join(PARENT_DIR, "normalised_data/min_max_values.json")

# Add parent dir to the path to import shared modules
sys.path.append(PARENT_DIR)

# Import existing data generator
from data_generator import DeepTraderDataGenerator, BATCHSIZE, NUMBER_OF_FEATURES, NUMBER_OF_STEPS

class DeepTraderXGB:
    """XGBoost model for DeepTrader that uses the same data as LSTM"""

    def __init__(self, model_name="DeepTrader_XGB"):
        """Initialize the XGBoost model."""
        self.model_name = model_name
        
        # Default parameters
        self.params = {
            'objective': 'reg:squarederror',
            'learning_rate': 0.05,
            'max_depth': 6,
            'min_child_weight': 1,
            'subsample': 0.8,
            'colsample_bytree': 0.8,
            'gamma': 0,
            'n_estimators': 100,
            'random_state': 42,
            'eval_metric': 'rmse', 
            'early_stopping_rounds': 10
        }

        # Create the model
        self.model = xgb.XGBRegressor(**self.params)
        
        # For storing training results
        self.history = None
        
    def train(self, data_generator, early_stopping_rounds=10):
        """Train the model using the same data generator as LSTM."""
        print("Preparing data from the generator...")
        
        # Collect all data from the generator
        X_train = []
        y_train = []
        
        # Process the same batches as LSTM would
        for i in range(len(data_generator)):
            X_batch, y_batch = data_generator[i]
            
            # Reshape from LSTM format (batch, time_steps, features) to XGBoost format (batch, features)
            for j in range(X_batch.shape[0]):
                # Take only the last time step for each sample
                X_train.append(X_batch[j, -1, :])
                y_train.append(y_batch[j, 0])
        
        # Convert to numpy arrays
        X_train = np.array(X_train)
        y_train = np.array(y_train)
        
        print(f"Prepared data shape - X: {X_train.shape}, y: {y_train.shape}")
        
        # Split into training and validation (80/20)
        split_idx = int(0.8 * len(X_train))
        X_val = X_train[split_idx:]
        y_val = y_train[split_idx:]
        X_train = X_train[:split_idx]
        y_train = y_train[:split_idx]
        
        print(f"Training data shape: {X_train.shape}")
        print(f"Validation data shape: {X_val.shape}")
        
        # Train the model
        print("Training XGBoost model...")
        self.model.fit(
            X_train, y_train,
            eval_set=[(X_train, y_train), (X_val, y_val)],
            verbose=True
        )
        
        # Store the evaluation results
        self.history = self.model.evals_result()
        
        return self.history
    
    def save_model(self):
        """Save the trained model."""
        # Create model directory
        os.makedirs(XGB_MODEL_DIR, exist_ok=True)
        
        # Save XGBoost model
        model_path = os.path.join(XGB_MODEL_DIR, f"{self.model_name}.json")
        self.model.save_model(model_path)
        
        # Also save as pickle for backup
        pickle_path = os.path.join(XGB_MODEL_DIR, f"{self.model_name}.pkl")
        with open(pickle_path, 'wb') as f:
            pickle.dump(self.model, f)
        
        # Save parameters
        params_path = os.path.join(XGB_MODEL_DIR, "params.json")
        with open(params_path, 'w') as f:
            json.dump(self.params, f, indent=2)
        
        # Verify that shared normalisation values exist 
        self.verify_normalisation_values()
        
        print(f"Model saved to {XGB_MODEL_DIR}/")
    
    def verify_normalisation_values(self):
        """Verify that shared normalisation values exist."""
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
    
    def plot_training_history(self):
        """Plot the training loss and metrics."""
        if self.history is None:
            print("No training history available")
            return
        
        # Create a new figure with controlled size
        fig = plt.figure(figsize=(12, 10))
        
        # Check which metrics are available in the history
        available_metrics = []
        for dataset in self.history.keys():
            available_metrics.extend(self.history[dataset].keys())
        available_metrics = list(set(available_metrics))  # Remove duplicates
        
        print(f"Available metrics in history: {available_metrics}")
        
        # Plot each available metric
        for i, metric in enumerate(available_metrics):
            ax = fig.add_subplot(len(available_metrics), 1, i+1)
            for dataset in self.history.keys():
                if metric in self.history[dataset]:
                    label = f"{dataset} {metric}"
                    ax.plot(self.history[dataset][metric], label=label)
            ax.set_xlabel("Boosting Iteration")
            ax.set_ylabel(metric.upper())
            ax.set_title(f"XGBoost {metric.upper()}")
            ax.legend()
        
        fig.tight_layout()
        
        # Save the plot to the model directory
        plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        
        fig.savefig(os.path.join(plots_dir, "training_history_xgb.png"))
        plt.close(fig)  # Close the figure to prevent it from showing
        
        # Now display the saved image
        saved_img = plt.imread(os.path.join(plots_dir, "training_history_xgb.png"))
        plt.figure(figsize=(10, 6))
        plt.imshow(saved_img)
        plt.axis('off')
        plt.show()
        
    def plot_feature_importance(self):
        """Plot feature importance."""
        fig = plt.figure(figsize=(10, 6))
        ax = fig.add_subplot(111)
        
        xgb.plot_importance(self.model, max_num_features=20, importance_type='gain', ax=ax)
        ax.set_title("XGBoost Feature Importance")
        
        # Save the plot to the model directory
        plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        
        fig.savefig(os.path.join(plots_dir, "feature_importance.png"))
        plt.close(fig)  # Close the figure to prevent it from showing
        
        # Now display the saved image
        saved_img = plt.imread(os.path.join(plots_dir, "feature_importance.png"))
        plt.figure(figsize=(10, 6))
        plt.imshow(saved_img)
        plt.axis('off')
        plt.show()

def main():
    # Define model input shape - same as LSTM
    input_shape = (BATCHSIZE, NUMBER_OF_STEPS, NUMBER_OF_FEATURES)

    # Create data generator - reuse the same one as LSTM
    data_path = os.path.join(PARENT_DIR, "normalised_data/normalised_data.pkl")
    data_generator = DeepTraderDataGenerator(data_path)
    
    # Create and train XGBoost model
    xgb_model = DeepTraderXGB()
    
    # Train the model using the same data generator
    history = xgb_model.train(data_generator)
    
    # Save and visualize the results
    xgb_model.save_model()
    xgb_model.plot_training_history()
    xgb_model.plot_feature_importance()
    
    print("XGBoost model training complete.")

if __name__ == "__main__":
    main()