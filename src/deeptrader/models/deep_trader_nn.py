# deep_trader_nn.py
"""
Neural network interface for DeepTrader C++ integration
"""

import os
import json
import numpy as np
import tensorflow as tf
from tensorflow.keras.models import load_model

def load_network(filename):
    """Load the neural network model"""
    try:
        return load_model(filename)
    except Exception as e:
        print(f"Error loading model: {e}")
        raise
    
def normalisation_values(filename):
    """Load normalisation min/max values from the model directory"""
    directory = os.path.dirname(filename)
    json_path = os.path.join(directory, 'min_max_values.json')
    
    with open(json_path, 'r') as f:
        data = json.load(f)
        
    return np.array(data['max_values']), np.array(data['min_values'])

def create_input(lob, otype): # Receives lob and order type from C++ side
    """Create the input for the model (identical to the original implementation)"""

    """ LOB dictionary containing current limit order book state i.e. timestamp, bids and asks (price and volume), record of recent trades (tape)"""
    time = lob["t"]
    bids = lob["bids"]
    asks = lob["asks"]
    tape = lob["tape"]
    smiths_alpha = 0
    p_estimate = 0
    delta_t = 0
    trade_type = 1 if otype == "Ask" else 0

    if len(tape) > 0:
        tape = reversed(tape)
        trades = [t for t in tape if t["type"] == "Trade"]
        trade_prices = [t["price"] for t in trades]

        if len(trade_prices) != 0:
            weights = np.power(0.9, np.arange(len(trade_prices)))
            p_estimate = np.average(trade_prices, weights=weights)
            smiths_alpha = np.sqrt(np.mean(np.square(trade_prices - p_estimate)))

            if time == trades[0]["t"]:
                trade_prices = trade_prices[1:]
                delta_t = trades[0]["t"] - (
                    0 if len(trades) == 1 else trades[1]["t"]
                )
    else:
        delta_t = time

    x = bids["best"] or 0
    y = asks["best"] or 0
    n_x, n_y = bids["n"], asks["n"]
    total = n_x + n_y

    mid_price = (x + y) / 2
    micro_price = ((n_x * y) + (n_y * x)) / total if total != 0 else 0
    imbalances = (n_x - n_y) / total if total != 0 else 0

    market_conditions = np.array(
        [
            time,         # timestamp 
            delta_t,      # time since last trade (elapsed time)
            trade_type,   # trade type (1: Bid, 0: Ask)
            x,            # best bid price
            y,            # best ask price 
            micro_price,  # micro price
            mid_price,    # mid price
            imbalances,   # imbalances
            abs(y - x),   # spread
            total,        # total volume
            p_estimate,   # price equlibrium estimate
            smiths_alpha, # smiths alpha
            0,            # limit price (set by C++ side)
        ]
    )
    return market_conditions