import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import yfinance as yf
from matplotlib.ticker import AutoMinorLocator
from datetime import timedelta

# Download Apple stock data for 2024-2025
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01")

def calculate_rolling_vwap(df, window=20):
    """
    Calculate Rolling Volume-Weighted Average Price (VWAP) without modifying df
    """
    # Calculate typical price for each day (high + low + close)/3
    typical_price = (df['High'] + df['Low'] + df['Close']) / 3
    
    # Calculate price * volume
    price_volume = typical_price * df['Volume']
    
    # Calculate rolling sum of price * volume and volume
    rolling_price_volume = price_volume.rolling(window=window).sum()
    rolling_volume = df['Volume'].rolling(window=window).sum()
    
    # Calculate rolling VWAP
    rolling_vwap = rolling_price_volume / rolling_volume
    
    return rolling_vwap

# Calculate VWAP with a 20-day rolling window
rolling_vwap = calculate_rolling_vwap(data, window=20)

# Create a figure
fig, ax1 = plt.subplots(figsize=(12, 4))

# Create a secondary y-axis for the volume
ax2 = ax1.twinx()

# Plot price and VWAP on the primary axis
ax1.plot(data.index, data['Close'], label='AAPL Close Price', color='blue', linewidth=2)
ax1.plot(data.index, rolling_vwap, label='Rolling VWAP (20-day)', color='purple', linewidth=2)

# Plot volume on the secondary axis (using bar plot)
# Scale down the volume bars to not dominate the chart
volume_scale = 0.3
ax2.bar(data.index, data['Volume'] * volume_scale, color='gray', alpha=0.3, label='Volume')
ax2.set_ylabel('Volume', color='gray')
ax2.tick_params(axis='y', labelcolor='gray')

# Find crossovers between price and VWAP
# Convert to numpy for easier comparison
close_array = data['Close'].values.flatten()
vwap_array = rolling_vwap.values.flatten()
crossover_indices = []
crossover_types = []

for i in range(1, len(close_array)):
    # Skip NaN values at the beginning (due to rolling window)
    if np.isnan(vwap_array[i]) or np.isnan(vwap_array[i-1]):
        continue
        
    # Price crosses above VWAP (bullish)
    if close_array[i-1] < vwap_array[i-1] and close_array[i] > vwap_array[i]:
        crossover_indices.append(i)
        crossover_types.append('bullish')
    
    # Price crosses below VWAP (bearish)
    elif close_array[i-1] > vwap_array[i-1] and close_array[i] < vwap_array[i]:
        crossover_indices.append(i)
        crossover_types.append('bearish')

# Filter signals to ensure minimum spacing (45 days)
min_days_between_signals = 45
filtered_bullish_indices = []
filtered_bearish_indices = []
last_bullish_date = None
last_bearish_date = None

# Process in chronological order
for i, signal_type in zip(crossover_indices, crossover_types):
    current_date = data.index[i]
    
    if signal_type == 'bullish':
        if last_bullish_date is None or (current_date - last_bullish_date).days >= min_days_between_signals:
            filtered_bullish_indices.append(i)
            last_bullish_date = current_date
    else:  # bearish
        if last_bearish_date is None or (current_date - last_bearish_date).days >= min_days_between_signals:
            filtered_bearish_indices.append(i)
            last_bearish_date = current_date

# Take first 3 of each maximum
bullish_indices = filtered_bullish_indices[:3]
bearish_indices = filtered_bearish_indices[:3]

# Add annotations for bullish crossovers
for i, idx in enumerate(bullish_indices):
    date = data.index[idx]
    price = close_array[idx]
    ax1.plot(date, price, 'go', markersize=8)  # Green dot
    
    # Custom positions for each bullish signal
    if i == 0:  # First bullish signal
        xytext = (10, 30)
    elif i == 1:  # Second bullish signal
        xytext = (-30, 30)
    else:  # Third bullish signal
        xytext = (10, 30)
    
    ax1.annotate('Bullish Signal', 
                xy=(date, price),
                xytext=xytext,
                textcoords='offset points',
                arrowprops=dict(facecolor='green', width=1.5, headwidth=7),
                fontsize=9,
                color='green',
                weight='bold')

# Add annotations for bearish crossovers
for i, idx in enumerate(bearish_indices):
    date = data.index[idx]
    price = close_array[idx]
    ax1.plot(date, price, 'ro', markersize=8)  # Red dot
    
    # Custom positions for each bearish signal
    if i == 0:  # First bearish signal
        xytext = (-80, -30)
    elif i == 1:  # Second bearish signal
        xytext = (-50, -20)
    else:  # Third bearish signal
        xytext = (8, -35)
    
    ax1.annotate('Bearish Signal', 
                xy=(date, price),
                xytext=xytext,
                textcoords='offset points',
                arrowprops=dict(facecolor='red', width=1.5, headwidth=7),
                fontsize=9,
                color='red',
                weight='bold')
    
# Add labels and grid
ax1.set_xlabel('Date', fontsize=12)
ax1.set_ylabel('Price ($)', fontsize=12)
ax1.grid(True, alpha=0.3)

# Combine legends from both axes
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')

# Format x-axis to show dates clearly
plt.gcf().autofmt_xdate()
plt.tight_layout()

# Save the figure
plt.savefig('aapl_rolling_vwap_with_volume.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()