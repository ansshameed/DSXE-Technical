import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from ta.momentum import RSIIndicator
import yfinance as yf
import random

# Download data
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01")
prices = data["Close"].squeeze()  # Convert to 1D Series

# Compute standard RSI (14-period)
rsi = RSIIndicator(close=prices, window=14).rsi()

# Calculate Stochastic RSI (simplified from research paper)
def calculate_stochastic_rsi(rsi_values, k_period=10, d_period=3):
    stoch_rsi = pd.Series(index=rsi_values.index, dtype=float)
    
    # Calculate StochRSI values
    for i in range(k_period, len(rsi_values)):
        rsi_window = rsi_values[i-k_period+1:i+1]
        if max(rsi_window) - min(rsi_window) != 0:
            stoch_rsi[i] = 100 * (rsi_values[i] - min(rsi_window)) / (max(rsi_window) - min(rsi_window))
        else:
            stoch_rsi[i] = 50
    
    # Smooth StochRSI with EMA
    alpha = 2 / (d_period + 1)
    smoothed = stoch_rsi.copy()
    first_valid_idx = stoch_rsi.first_valid_index()
    
    if first_valid_idx is not None:
        current_idx = stoch_rsi.index.get_loc(first_valid_idx)
        for i in range(current_idx + 1, len(stoch_rsi)):
            if pd.notna(stoch_rsi[i]) and pd.notna(smoothed[i-1]):
                smoothed[i] = alpha * stoch_rsi[i] + (1 - alpha) * smoothed[i-1]
    
    return smoothed

# Calculate smoothed StochRSI
smoothed_stoch_rsi = calculate_stochastic_rsi(rsi)

# Create a compact 2-panel figure
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True, gridspec_kw={'height_ratios': [1, 1]})

# Create a twin axis for prices on the RSI subplot
ax_price = ax1.twinx()

# Plot RSI on first subplot
ax1.plot(rsi.index, rsi, label="RSI (14-period)", color="purple")
ax1.axhline(70, color="red", linestyle="--", label="70")
ax1.axhline(30, color="green", linestyle="--", label="30")
ax1.fill_between(rsi.index, 70, 100, color="red", alpha=0.1)
ax1.fill_between(rsi.index, 0, 30, color="green", alpha=0.1)
mid_date = rsi.index[len(rsi)//2]
ax1.text(mid_date, 90, "Overbought Zone", color="red", 
         fontsize=9, ha="center", va="center", 
         bbox=dict(facecolor="white", alpha=0.7, pad=2))
ax1.text(mid_date, 15, "Oversold Zone", color="green", 
         fontsize=9, ha="center", va="center", 
         bbox=dict(facecolor="white", alpha=0.7, pad=2))
ax1.set_ylabel("RSI", color="purple")
ax1.tick_params(axis='y', labelcolor="purple")
ax1.grid(True, alpha=0.3)
ax1.set_ylim(0, 100)

# Plot price on the twin axis
ax_price.plot(prices.index, prices, label="AAPL Price", color="blue", alpha=0.7)
ax_price.set_ylabel("Price ($)", color="blue")
ax_price.tick_params(axis='y', labelcolor="blue")

# Combine legends from both axes
lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax_price.get_legend_handles_labels()
ax1.legend(lines2 + lines1, labels2 + labels1, loc="upper left", fontsize=8)

# Plot Stochastic RSI on bottom subplot
ax2.plot(smoothed_stoch_rsi.index, smoothed_stoch_rsi, label="Smoothed StochRSI", color="blue")
ax2.axhline(80, color="red", linestyle="--", label="80")
ax2.axhline(20, color="green", linestyle="--", label="20")
ax2.fill_between(smoothed_stoch_rsi.index, 80, 100, color="red", alpha=0.1)
ax2.fill_between(smoothed_stoch_rsi.index, 0, 20, color="green", alpha=0.1)
ax2.set_ylabel("Stochastic RSI")
ax2.legend(loc="upper left", fontsize=8)
ax2.grid(True, alpha=0.3)
ax2.set_ylim(0, 100)
ax2.set_xlabel("Date")

# Annotate key signals on both charts (limited number for readability)
events = []
for i in range(1, len(smoothed_stoch_rsi)):
    prev, curr = smoothed_stoch_rsi.iloc[i-1], smoothed_stoch_rsi.iloc[i]
    date, val  = smoothed_stoch_rsi.index[i], smoothed_stoch_rsi.iloc[i]
    if prev <= 80 < curr:
        events.append((i, date, val, 'Overbought', 'red', +12))
    elif prev >= 20 > curr:
        events.append((i, date, val, 'Oversold', 'green', -15))

# Drop any that occur in the first 10% of your data
cutoff = int(len(smoothed_stoch_rsi) * 0.1)
events = [e for e in events if e[0] > cutoff]

# Randomly select up to 2 events
random.seed(42)
chosen = random.sample(events, min(2, len(events)))

# Annotate only those
for _, date, val, label, color, dy in chosen:
    ax2.annotate(
        label,
        xy=(date, val),
        xytext=(date, val + dy),
        arrowprops=dict(facecolor=color, width=1, headwidth=5),
        fontsize=8, ha='center', color=color
    )

# Format date axis and layout
plt.gcf().autofmt_xdate()
plt.tight_layout()
plt.savefig('aapl_rsi_with_price_chart.png', dpi=300, bbox_inches='tight')
plt.show()