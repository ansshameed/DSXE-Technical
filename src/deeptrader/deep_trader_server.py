#!/usr/bin/env python3
"""
DeepTrader Python Server
"""

import socket
import json
import numpy as np
import sys
import os
import time
from pathlib import Path

# Get the absolute path of the current script
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(os.path.dirname(current_dir))  # Up two levels

# Add paths
sys.path.append(parent_dir)  # Root project dir
sys.path.append(current_dir)  # Current dir
sys.path.append(os.path.join(current_dir, 'models'))  # Modles dir

# Direct import
sys.path.insert(0, os.path.join(current_dir, 'models'))

try:
    import deep_trader_nn

except ImportError:
    print("Couldn't import deep_trader_nn directly, looking in other locations...")

    try:
        from models import deep_trader_nn
    except ImportError:
        print("Failed to import from models too. Creating empty module for testing...")
        
        # Create a simple stub for testing
        class MockModule:
            @staticmethod
            def load_network(path):
                print(f"Mock: Loading network from {path}")
                return MockModel()
                
            @staticmethod
            def normalisation_values(path):
                print(f"Mock: Getting normalisation values from {path}")
                return [np.array([1.0] * 14), np.array([0.0] * 14)]
                
            @staticmethod
            def create_input(lob, otype):
                print(f"Mock: Creating input for {otype}")
                return np.array([0.5] * 13)
        
        class MockModel:
            def __call__(self, input_data):
                print(f"Mock: Model called with shape {input_data.shape}")
                return np.array([0.5])
                
            def numpy(self):
                return self
                
            def item(self):
                return 0.5
                
        deep_trader_nn = MockModule()

# Load the model - adjust path based on actual location
MODEL_PATH = os.path.join(current_dir, "models/DeepTrader_LSTM/DeepTrader_LSTM.keras")

if not os.path.exists(MODEL_PATH):
    MODEL_PATH = os.path.join(parent_dir, "src/deeptrader/models/DeepTrader_LSTM/DeepTrader_LSTM.keras")

HOST = '127.0.0.1'
PORT = 8777

# Try to load the model
try:
    print(f"Loading model from: {MODEL_PATH}")
    print(f"deep_trader_nn module located at: {deep_trader_nn.__file__ if hasattr(deep_trader_nn, '__file__') else 'mock module'}")
    model = deep_trader_nn.load_network(MODEL_PATH)
    min_max = deep_trader_nn.normalisation_values(MODEL_PATH)
    max_values = min_max[0]
    min_values = min_max[1]
    print("Model loaded successfully")
except Exception as e:
    print(f"Error loading model: {e}")
    print("Using mock values for testing")
    max_values = np.array([1.0] * 14)
    min_values = np.array([0.0] * 14)

def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((HOST, PORT))
    server_socket.listen(5)
    
    print(f"Server started on {HOST}:{PORT}")
    
    try:
        while True:
            client_socket, addr = server_socket.accept()
            print(f"Connection from {addr}")
            
            # Handle client request
            handle_client(client_socket)
    except KeyboardInterrupt:
        print("Server shutting down")
    finally:
        server_socket.close()

def handle_client(client_socket):
    try:
        # Receive data
        data = client_socket.recv(4096).decode('utf-8')
        if not data:
            return
        
        print(f"Received data: {data}")
        
        # Parse JSON
        request = json.loads(data)
        request_type = request.get('type')
        
        if request_type == 'predict':
            # Extract data from request
            order_type = request.get('order_type')
            
            # Make prediction using the provided values
            try:
                # Use the provided values directly instead of recalculating
                time = request.get('timestamp', 0)
                time_diff = request.get('time_diff', 0)
                side = request.get('trade_type', 1 if order_type == "Ask" else 0)
                best_bid = request.get('best_bid', 0)
                best_ask = request.get('best_ask', 0)
                micro_price = request.get('micro_price', 0)
                mid_price = request.get('mid_price', 0)
                imbalances = request.get('imbalance', 0)
                spread = request.get('spread', 0)
                total_volume = request.get('total_volume', 0)
                p_equilibrium = request.get('p_equilibrium', 0)
                smiths_alpha = request.get('smiths_alpha', 0)
                limit_price = request.get('limit_price', 150.0)
                
                # Create features in the exact same order as the original code
                features = np.array([
                    time,
                    time_diff,
                    side,
                    best_bid,
                    best_ask,
                    micro_price,
                    mid_price,
                    imbalances,
                    spread,
                    total_volume,
                    p_equilibrium,
                    smiths_alpha,
                    limit_price,
                ])
                
                print(f"Features: {features}")
                print(f"Min values: {min_values[:13]}")
                print(f"Max values: {max_values[:13]}")
                
                # Normalise exactly as in the original code
                normalized_input = (features - min_values[:13]) / (max_values[:13] - min_values[:13])
                
                # Reshape for LSTM
                normalized_input = np.reshape(normalized_input, (1, 1, -1))
                
                # Predict
                normalized_output = model(normalized_input).numpy().item()
                
                # Denormalize exactly as in the original code
                denormalized_output = (normalized_output * (max_values[13] - min_values[13])) + min_values[13]
                model_price = int(round(denormalized_output, 0))
                
                print(f"Normalized output: {normalized_output}, Denormalized: {denormalized_output}, Final: {model_price}")
                
                # Apply the same price adjustments as the original implementation
                if order_type == "Ask":
                    if model_price < limit_price:
                        model_price = limit_price + 1
                        if limit_price < best_ask - 1:
                            model_price = best_ask - 1
                else:
                    if model_price > limit_price:
                        model_price = limit_price - 1
                        if limit_price > best_bid + 1:
                            model_price = best_bid + 1
                
                # Add sanity check
                if model_price < 50 or model_price > 200:
                    print(f"Warning: Unreasonable prediction: {model_price}, using fallback")
                    if order_type == "Ask":
                        model_price = best_ask - 1
                    else:
                        model_price = best_bid + 1
                
                final_price = model_price
                print(f"Final prediction: {final_price} for {order_type}")
                
                # Send response
                response = {
                    'status': 'success',
                    'price': final_price
                }

            except Exception as e:
                print(f"Prediction error: {e}")
                # Fallback as before
                if order_type == 'Ask':
                    price = max(request['best_bid'] + 1, request['best_ask'] - 1)
                else:
                    price = min(request['best_ask'] - 1, request['best_bid'] + 1)
                    
                response = {
                    'status': 'error',
                    'price': price,
                    'error': str(e)
                }
        else:
            response = {
                'status': 'error',
                'error': f'Unknown request type: {request_type}'
            }
            
        print(f"Sending response: {response}")
        client_socket.sendall(json.dumps(response).encode('utf-8'))
        
    except Exception as e:
        print(f"Error handling client: {e}")
    finally:
        client_socket.close()

if __name__ == "__main__":
    # Print diagnostic information
    print(f"Python version: {sys.version}")
    print(f"Current directory: {os.getcwd()}")
    print(f"Model path exists: {os.path.exists(MODEL_PATH)}")
    
    # Start the server
    start_server()