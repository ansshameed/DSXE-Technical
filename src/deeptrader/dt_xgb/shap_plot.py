#!/usr/bin/env python3
"""
Generate SHAP plot for existing XGBoost model
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt
import pickle
import shap

# Get the current directory and add parent to path to access shared modules
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)  # DeepTrader dir
XGB_MODEL_DIR = os.path.join(SCRIPT_DIR, "xgb_models")

# Add parent dir to the path to import shared modules
sys.path.append(PARENT_DIR)

# Import existing data generator
from data_generator import DeepTraderDataGenerator

def generate_shap_plot():
    """Generate SHAP plot for the existing XGBoost model"""
    
    # Define feature names (same as in your original script)
    feature_names = ["timestamp", "time_diff", "side", "best_bid", "best_ask", 
                    "micro_price", "mid_price", "imbalance", "spread", 
                    "total_volume", "p_equilibrium", "smiths_alpha", "limit_price_chosen"]
    
    # Load the saved model
    pickle_path = os.path.join(XGB_MODEL_DIR, "DeepTrader_XGB.pkl")
    with open(pickle_path, 'rb') as f:
        model = pickle.load(f)
    
    print("Model loaded successfully")
    
    # Load a sample of data for SHAP analysis
    data_path = os.path.join(PARENT_DIR, "normalised_data/normalised_data.pkl")
    data_generator = DeepTraderDataGenerator(data_path)
    
    print("Getting sample data for SHAP analysis")
    X_batch, y_batch = data_generator[0]  # Get first batch
    X_sample = np.array([X_batch[i, -1, :] for i in range(X_batch.shape[0])])
    
    # Limit sample size for faster plotting
    if len(X_sample) > 100:
        X_sample = X_sample[:100]
    
    print(f"Generating SHAP values for {len(X_sample)} samples")
    
    # Create explainer and get SHAP values
    explainer = shap.TreeExplainer(model)
    shap_values = explainer.shap_values(X_sample)
    
    # Create plot with improved formatting
    plt.figure(figsize=(14, 8))  # Wider figure
    shap.summary_plot(shap_values, X_sample, feature_names=feature_names, show=False)
    
    # Save with tight layout and extra padding
    plots_dir = os.path.join(XGB_MODEL_DIR, "plots")
    os.makedirs(plots_dir, exist_ok=True)
    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, "shap_summary_fixed.png"), 
                bbox_inches='tight', 
                pad_inches=0.5)
    
    print(f"SHAP plot saved to {os.path.join(plots_dir, 'shap_summary_fixed.png')}")
    
    # Display saved image
    saved_img = plt.imread(os.path.join(plots_dir, "shap_summary_fixed.png"))
    plt.figure(figsize=(14, 8))
    plt.imshow(saved_img)
    plt.axis('off')
    plt.show()

if __name__ == "__main__":
    generate_shap_plot()