import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import yfinance as yf
from scipy.stats import norm
from matplotlib.ticker import AutoMinorLocator
import random

# Download Apple stock data for 2024-2025
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01", auto_adjust=True)

def calculate_obv_delta(df, lookback_period=20, delta_period=5):
    df = df.copy()
    price_change = df['Close'].diff()
    price_direction = price_change.apply(np.sign)
    signed_volume   = price_direction * df['Volume']
    rolling_signed = signed_volume.rolling(window=lookback_period).sum()
    rolling_total  = df['Volume'].rolling(window=lookback_period).sum()
    ratio          = rolling_signed / rolling_total
    scaled_ratio   = ratio * np.sqrt(lookback_period)

    # Normalize via map (keeps it 1‑D)
    compression_factor = 0.6
    norm_obv = scaled_ratio.map(
        lambda x: 100 * norm.cdf(compression_factor * x) - 50
    )

    delta_obv = norm_obv - norm_obv.shift(delta_period)
    return delta_obv

# Calculate OBV Delta
obv_delta = calculate_obv_delta(data)
obv_delta = calculate_obv_delta(data).squeeze()

# Create figure
fig, ax1 = plt.subplots(figsize=(12, 4))

# Add a secondary axis for the OBV Delta
ax2 = ax1.twinx()

# Plot price on primary axis
ax1.plot(data.index, data['Close'], label='AAPL Close Price', color='blue', linewidth=2)
ax1.set_xlabel('Date', fontsize=12)
ax1.set_ylabel('Price ($)', fontsize=12, color='blue')
ax1.tick_params(axis='y', labelcolor='blue')
ax1.grid(True, alpha=0.3)

# Plot OBV Delta on secondary axis
ax2.plot(data.index, obv_delta, label='OBV Delta', color='purple', linewidth=2)
ax2.set_ylabel('OBV Delta', fontsize=12, color='purple')
ax2.tick_params(axis='y', labelcolor='purple')

# Find crossover signals
buy_signals = []
sell_signals = []

for i in range(1, len(obv_delta)):
    prev = obv_delta.iloc[i-1]
    curr = obv_delta.iloc[i]
    if pd.isna(prev) or pd.isna(curr):
        continue

    if prev < 0 and curr >= 0:
        buy_signals.append(i)
    if prev > 0 and curr <= 0:
        sell_signals.append(i)

# Filter signals for spacing (minimum 30 days between signals)
min_days_between_signals = 30
filtered_buy_signals = []
filtered_sell_signals = []
last_buy_date = None
last_sell_date = None

random.seed(42)
selected_buys  = random.sample(buy_signals,  min(3, len(buy_signals)))
selected_sells = random.sample(sell_signals, min(3, len(sell_signals)))
buy_offsets  = [(-60,40), (20,-30), (  5, 50)]   # as many tuples as you have buys
sell_offsets = [(70,20), (-50,-30), (20, 50)] 

for (idx, (dx, dy)) in zip(selected_buys, buy_offsets):
    date  = data.index[idx]
    price = data['Close'].iloc[idx]
    ax1.plot(date, price, 'go', markersize=8)
    ax1.annotate(
        'Buy Signal',
        xy=(date, price),
        xytext=(dx, dy),
        textcoords='offset points',
        arrowprops=dict(facecolor='green', headwidth=7),
        color='green', weight='bold'
    )

for (idx, (dx, dy)) in zip(selected_sells, sell_offsets):
    date  = data.index[idx]
    price = data['Close'].iloc[idx]
    ax1.plot(date, price, 'ro', markersize=8)
    ax1.annotate(
        'Sell Signal',
        xy=(date, price),
        xytext=(dx, dy),
        textcoords='offset points',
        arrowprops=dict(facecolor='red', headwidth=7),
        color='red', weight='bold'
    )

# Combine legends from both axes
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')

# Format date axis
plt.gcf().autofmt_xdate()
plt.tight_layout()

# Save the figure
plt.savefig('aapl_obv_delta_chart.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()