import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import yfinance as yf
from matplotlib.ticker import AutoMinorLocator

# Download Apple stock data for 2024-2025
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01")

# Calculate Bollinger Bands
def calculate_bollinger_bands(prices, window=20, num_std=2):
    """
    Calculate Bollinger Bands
    
    Parameters:
    - prices: price series
    - window: lookback period (default 20)
    - num_std: number of standard deviations (default 2)
    """
    # Ensure prices is 1D
    prices = np.array(prices).flatten()
    
    # Calculate middle band (SMA)
    middle_band = pd.Series(prices).rolling(window=window).mean()
    
    # Calculate standard deviation
    std = pd.Series(prices).rolling(window=window).std()
    
    # Calculate upper and lower bands
    upper_band = middle_band + (std * num_std)
    lower_band = middle_band - (std * num_std)
    
    return middle_band, upper_band, lower_band

# Get Close prices
prices = data['Close'].values.flatten()
dates = data.index

# Calculate Bollinger Bands (20-period, 2 standard deviations)
middle_band, upper_band, lower_band = calculate_bollinger_bands(prices, window=20, num_std=2)

# Create a figure
plt.figure(figsize=(12, 4))

# Plot price and Bollinger Bands
plt.plot(dates, prices, label='AAPL Close Price', color='blue', linewidth=2)
plt.plot(dates, middle_band, label='Middle Band (20-day SMA)', color='orange', linewidth=1.5)
plt.plot(dates, upper_band, label='Upper Band (SMA+2σ)', color='darkred', linewidth=1.5, linestyle='--')
plt.plot(dates, lower_band, label='Lower Band (SMA-2σ)', color='darkgreen', linewidth=1.5, linestyle='--')

# Fill the area between upper and lower bands with a more subtle color
plt.fill_between(dates, upper_band, lower_band, color='lightblue', alpha=0.2)

# Find significant band crosses (not just touches) and filter to limit the number
upper_crosses = []
lower_crosses = []

# Looking for more significant crosses, not just touches
for i in range(1, len(prices)):
    # Only consider after bands are established (after window period)
    if i >= 20:
        # For upper band: price crosses from below to above
        if prices[i-1] < upper_band[i-1] and prices[i] > upper_band[i]:
            upper_crosses.append(i)
        
        # For lower band: price crosses from above to below
        if prices[i-1] > lower_band[i-1] and prices[i] < lower_band[i]:
            lower_crosses.append(i)

# Limit to a maximum of 3 signals of each type
max_signals = 3
if len(upper_crosses) > max_signals:
    # Keep only every nth signal to get max_signals
    step = len(upper_crosses) // max_signals
    upper_crosses = upper_crosses[::step][:max_signals]

if len(lower_crosses) > max_signals:
    step = len(lower_crosses) // max_signals
    lower_crosses = lower_crosses[::step][:max_signals]

# Add annotations for upper band crosses (limited)
for idx in upper_crosses:
    date = dates[idx]
    price = prices[idx]
    plt.plot(date, price, 'ro', markersize=8)  # Red dot
    plt.annotate('Overbought', 
                xy=(date, price),
                xytext=(-10, 40),
                textcoords='offset points',
                arrowprops=dict(facecolor='darkred', width=1.5, headwidth=7),
                fontsize=9,
                color='darkred',
                weight='bold')

# Add annotations for lower band crosses (limited)
for idx in lower_crosses:
    date = dates[idx]
    price = prices[idx]
    plt.plot(date, price, 'go', markersize=8)  # Green dot
    plt.annotate('Oversold', 
                xy=(date, price),
                xytext=(-50, -35),
                textcoords='offset points',
                arrowprops=dict(facecolor='darkgreen', width=1.5, headwidth=7),
                fontsize=9,
                color='darkgreen',
                weight='bold')

# Add labels and title
plt.xlabel('Date', fontsize=12)
plt.ylabel('Price ($)', fontsize=12)
plt.grid(True, alpha=0.3)
plt.legend(loc='upper left')

# Format x-axis to show dates clearly
plt.gcf().autofmt_xdate()
plt.tight_layout()

# Save the figure
plt.savefig('aapl_bollinger_bands.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()