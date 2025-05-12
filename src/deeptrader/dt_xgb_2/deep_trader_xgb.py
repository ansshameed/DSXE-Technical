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
import shap

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
            'n_estimators': 1000,
            'random_state': 42
        }

        # Create the model
        self.model = xgb.XGBRegressor(**self.params)
        
        # For storing training results
        self.history = None
        
    def train(self, data_generator):
        """Train the model using the same data generator as LSTM."""
        print("Preparing data from the generator...")
        
        # Collect all data from the generator
        X_train = []
        y_train = []
        
        # Process the same batches as LSTM would
        total_batches = len(data_generator)
        print(f"Total batches to process: {total_batches}")
        for i in range(len(data_generator)):
            if i % 100 == 0:  # Print every 100 batches
                print(f"Processing batch {i}/{total_batches} ({i/total_batches*100:.1f}%)")
            X_batch, y_batch = data_generator[i]
            
            # Reshape from LSTM format (batch, time_steps, features) to XGBoost format (batch, features)
            for j in range(X_batch.shape[0]):
                X_train.append(X_batch[j, -1, :])
                y_train.append(y_batch[j, 0])
        
        # Convert to numpy arrays
        X_train = np.array(X_train)
        y_train = np.array(y_train)
        
        print(f"Prepared data shape - X: {X_train.shape}, y: {y_train.shape}")
        
        print("Training XGBoost model...")
        # Modify to track training loss
        eval_set = [(X_train, y_train)]
        self.model.fit(
            X_train, y_train,
            eval_set=eval_set,
            verbose=True
        )
        
        # Store the evaluation results
        self.history = {
            'training': {
                'mse': self.model.evals_result()['validation_0']['rmse']
            }
        }
        
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
        """Plot the training loss."""
        if self.history is None or 'training' not in self.history:
            print("No training history available")
            return
        
        # Extract training loss (MSE)
        training_loss = self.history['training']['mse']
        
        # Create the plot
        plt.figure(figsize=(10, 6))
        plt.plot(training_loss, label='Training Loss (MSE)', color='blue')
        plt.title('XGBoost Training Loss')
        plt.xlabel('Boosting Iterations')
        plt.ylabel('Mean Squared Error (MSE)')
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        
        # Save the plot
        plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        plt.savefig(os.path.join(plots_dir, "training_loss.png"))
        
        # Display the plot
        plt.show()
        
    def plot_feature_importance(self, feature_names=None):
        """Plot feature importance with optional feature names."""
        # Get importance scores
        importance = self.model.get_booster().get_score(importance_type='gain')
        
        # Create a list of (feature_name, importance_value) tuples
        if feature_names is not None:
            # Map feature indices to names
            name_importance = []
            for key, value in importance.items():
                idx = int(key[1:])  # Extract the index from 'f0', 'f1', etc.
                if idx < len(feature_names):
                    name = feature_names[idx]
                    name_importance.append((name, value))
                else:
                    name_importance.append((key, value))
            
            # Sort by importance
            name_importance.sort(key=lambda x: x[1], reverse=True)
            
            # Take top 20 features
            top_features = name_importance[:20]
            
            # Separate names and values
            names = [x[0] for x in top_features]
            values = [x[1] for x in top_features]
            
            # Create custom plot with feature names
            fig, ax = plt.subplots(figsize=(10, 6))
            y_pos = range(len(names))
            ax.barh(y_pos, values, align='center')
            ax.set_yticks(y_pos)
            ax.set_yticklabels(names)
            ax.invert_yaxis()  # Highest values at the top
            ax.set_xlabel('Importance (Gain)')
            ax.set_title("XGBoost Feature Importance")
        else:
            # Use default XGBoost plotting if no feature names
            fig, ax = plt.subplots(figsize=(10, 6))
            xgb.plot_importance(self.model, max_num_features=20, importance_type='gain', ax=ax)
        
        # Save numerical values with proper names
        importance_dict = {}
        for key, value in importance.items():
            idx = int(key[1:])
            if feature_names is not None and idx < len(feature_names):
                importance_dict[feature_names[idx]] = value
            else:
                importance_dict[key] = value
        
        importance_path = os.path.join(XGB_MODEL_DIR, "feature_importance.json")
        with open(importance_path, 'w') as f:
            json.dump(importance_dict, f, indent=2)
        
        # Save the plot
        plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        fig.savefig(os.path.join(plots_dir, "feature_importance.png"))
        plt.close(fig)
        
        # Display saved image
        saved_img = plt.imread(os.path.join(plots_dir, "feature_importance.png"))
        plt.figure(figsize=(10, 6))
        plt.imshow(saved_img)
        plt.axis('off')
        plt.show()
        
        # Return the importance dict
        return importance_dict
    
    def plot_shap_values(self, X_sample, feature_names=None):
        """Plot SHAP values to show feature contributions."""

        # Create explainer and get SHAP values
        explainer = shap.TreeExplainer(self.model)
        shap_values = explainer.shap_values(X_sample)
        
        # Create plot
        plt.figure(figsize=(10, 8))
        shap.summary_plot(shap_values, X_sample, feature_names=feature_names, show=False)
        
        # Save plot
        plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
        os.makedirs(plots_dir, exist_ok=True)
        plt.savefig(os.path.join(plots_dir, "shap_summary.png"))
        plt.close()
        
        # Display saved image
        saved_img = plt.imread(os.path.join(plots_dir, "shap_summary.png"))
        plt.figure(figsize=(10, 8))
        plt.imshow(saved_img)
        plt.axis('off')
        plt.show()

def main():
    # Define model input shape - same as LSTM
    input_shape = (BATCHSIZE, NUMBER_OF_STEPS, NUMBER_OF_FEATURES)

    # Define feature names
    feature_names = ["timestamp", "time_diff", "side", "best_bid", "best_ask", 
                    "micro_price", "mid_price", "imbalance", "spread", 
                    "total_volume", "p_equilibrium", "smiths_alpha", "limit_price_chosen"]

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
    
    # Pass feature names to the plot_feature_importance method
    xgb_model.plot_feature_importance(feature_names)

    X_batch, y_batch = data_generator[0] 
    X_sample = np.array([X_batch[i, -1, :] for i in range(X_batch.shape[0])])
    #if len(X_sample) > 100: # Limit sample for SHAP for plotting speed
        #X_sample = X_sample[:100]
    #xgb_model.plot_shap_values(X_sample, feature_names)
    
    print("XGBoost model training complete.")

if __name__ == "__main__":
    main()