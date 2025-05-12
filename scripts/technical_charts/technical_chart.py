import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from ta.momentum import RSIIndicator
import yfinance as yf

# Download real price data for Apple (AAPL)
data = yf.download("AAPL", start="2023-01-01", end="2024-01-01")

# Use the closing price for RSI calculation (MUST be 1D Series)
prices = data["Close"].squeeze()  # <- Correct

# Compute RSI (14-period by default)
rsi = RSIIndicator(close=prices).rsi()

# Plot RSI with annotated thresholds
plt.figure(figsize=(12, 6))
plt.plot(rsi.index, rsi, label="RSI", color="blue")
plt.axhline(70, color="red", linestyle="--", label="Overbought (70)")
plt.axhline(30, color="green", linestyle="--", label="Oversold (30)")
plt.fill_between(rsi.index, 70, 100, color="red", alpha=0.1)
plt.fill_between(rsi.index, 0, 30, color="green", alpha=0.1)

# Optional: annotate last RSI value
last_rsi = rsi.dropna().iloc[-1]
last_date = rsi.dropna().index[-1]
plt.annotate(f"RSI: {last_rsi:.2f}", xy=(last_date, last_rsi),
             xytext=(last_date, last_rsi + 5),
             arrowprops=dict(facecolor='black', arrowstyle="->"),
             fontsize=10, ha='right')

plt.title("AAPL - Relative Strength Index (RSI) with Thresholds")
plt.xlabel("Date")
plt.ylabel("RSI Value")
plt.legend()
plt.tight_layout()
plt.show()
