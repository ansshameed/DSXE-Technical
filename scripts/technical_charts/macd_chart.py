import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import yfinance as yf
import matplotlib.dates as mdates
from matplotlib.ticker import AutoMinorLocator
from matplotlib.patches import Patch


# Download Apple stock data for 2024-2025
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01")

# Create a function to calculate EMA
def calculate_ema(prices, period):
    prices = np.array(prices, dtype=float)
    ema    = np.full_like(prices, np.nan)
    α      = 2 / (period + 1)

    # find first index where prices is not NaN
    valid = np.where(~np.isnan(prices))[0]
    if len(valid) < period:
        return ema  # not enough data

    start = valid[0] + period - 1
    # seed with the simple average of the first 'period' *valid* prices
    ema[start] = np.nanmean(prices[ valid[:period] ])  

    # now recurse forward
    for i in range(start + 1, len(prices)):
        ema[i] = α * prices[i] + (1 - α) * ema[i-1]

    return ema


# Get Close prices and ensure they're 1D
prices = data['Close'].values.flatten()
dates = data.index

# Calculate EMAs (equation 2.9)
ema_short = calculate_ema(prices, 12)  # 12-period EMA (short-term)
ema_long = calculate_ema(prices, 26)   # 26-period EMA (long-term)

# Calculate MACD Line (equation 2.10)
macd_line = ema_short - ema_long

# Calculate Signal Line (equation 2.11)
signal_line = calculate_ema(macd_line, 9)  # 9-period EMA of MACD

# Calculate Histogram (equation 2.12)
histogram = macd_line - signal_line

# Create a DataFrame for convenience
macd_data = pd.DataFrame({
    'Price': prices,
    'EMA12': ema_short,
    'EMA26': ema_long,
    'MACD': macd_line,
    'Signal': signal_line,
    'Histogram': histogram
}, index=dates)

# … your EMA/MACD/Histogram code above …

# 1️⃣ Build all raw crossovers in one pass
# … after you’ve built macd_data …

# 1️⃣ build every raw crossover exactly once
buy_signals  = []
sell_signals = []
for i in range(1, len(macd_data)):
    prev_h = macd_data['Histogram'].iat[i-1]
    cur_h  = macd_data['Histogram'].iat[i]
    if prev_h <= 0 and cur_h  > 0:
        buy_signals.append(i)
    if prev_h >= 0 and cur_h  < 0:
        sell_signals.append(i)

# 2️⃣ filter out the tiny wiggles
threshold = 0.5
buy_signals  = [i for i in buy_signals  if macd_data['Histogram'].iat[i]  >  threshold]
sell_signals = [i for i in sell_signals if macd_data['Histogram'].iat[i]  < -threshold]

# 3️⃣ enforce a minimum spacing between plotted arrows
from datetime import timedelta

def filter_by_spacing(signals, dates, min_days=30):
    if not signals: return []
    out  = [signals[0]]
    last = dates[signals[0]]
    for idx in signals[1:]:
        if (dates[idx] - last).days >= min_days:
            out.append(idx)
            last = dates[idx]
    return out

buy_signals  = filter_by_spacing(buy_signals, dates, min_days=30)
sell_signals = filter_by_spacing(sell_signals, dates, min_days=30)

# ——— NOW: plot your price, EMAs, and ONLY these final buy/sell indices ———

# Create a figure with 2 subplots sharing the x-axis
fig, (ax1, ax2, ax3) = plt.subplots(
    3, 1,
    figsize=(12, 6),
    sharex=True,
    gridspec_kw={'height_ratios': [1.5, 1, 0.8]}
)

# Plot the price and EMAs on the top subplot
ax1.plot(dates, prices, label='AAPL Close Price', color='blue', linewidth=2)
ax1.plot(dates, ema_short, label='EMA (12-period)', color='orange', linewidth=1.5)
ax1.plot(dates, ema_long, label='EMA (26-period)', color='purple', linewidth=1.5)
ax1.set_ylabel('Price ($)', fontsize=12)
ax1.grid(True, alpha=0.3)
ax1.legend(loc='upper left')

# Add dots at buy signals
for idx in buy_signals:
    date = dates[idx]
    price = prices[idx]
    ax1.plot(date, price, 'go', markersize=8)  # Green dot
    ax1.annotate('Buy Signal', 
                 xy=(date, price),
                 xytext=(-100, -30),
                 textcoords='offset points',
                 arrowprops=dict(facecolor='green', shrink=0.05, width=1.5),
                 fontsize=10,
                 color='green')

# Add dots at sell signals
for idx in sell_signals:
    date = dates[idx]
    price = prices[idx]
    ax1.plot(date, price, 'ro', markersize=8)  # Red dot
    ax1.annotate('Sell Signal', 
                 xy=(date, price),
                 xytext=(50, 30),
                 textcoords='offset points',
                 arrowprops=dict(facecolor='red', shrink=0.05, width=1.5),
                 fontsize=10,
                 color='red')

# CRITICAL FIX: Create separate axis for the histogram
# First, calculate the date spacing to set appropriate bar width
date_nums = mdates.date2num(dates)
if len(date_nums) > 1:
    avg_day_interval = (date_nums[-1] - date_nums[0]) / len(date_nums)
    bar_width = avg_day_interval * 0.8  # Make bars 80% of available space
else:
    bar_width = 1

pos_color = "#008000"   # a true green
neg_color = "#C00000"   # a true red

# Plot histogram with NO label
colors = [neg_color if h < 0 else pos_color for h in histogram]
ax2.bar(dates, histogram,
        color=colors,
        alpha=1.0,
        width=bar_width)

# Plot MACD & Signal lines (they carry their own labels)
line_macd, = ax2.plot(dates, macd_line,   label='MACD Line',   color='blue', linewidth=1.5)
line_sig,  = ax2.plot(dates, signal_line, label='Signal Line', color='orange', linewidth=1.5)
ax2.axhline(0, color='black', linestyle='-', alpha=1.0)

# Build legend proxies for your histogram
pos_patch = Patch(facecolor=pos_color, label='Histogram (positive)')
neg_patch = Patch(facecolor=neg_color, label='Histogram (negative)')

# Draw a combined legend
ax2.legend(
    handles=[line_macd, line_sig, pos_patch, neg_patch],
    loc='upper left',
    frameon=True,
    fontsize=9,        # smaller text
)
ymin, ymax = ax2.get_ylim()
text_y = ymax * 0.95   # 95% up the MACD pane
# — draw little arrows to the actual crossover points on the MACD panel —
# ── mark the actual crossover point with a dot + arrow ──
for idx in buy_signals:
    x = dates[idx]
    # mid‑point between MACD & Signal at the crossover
    y = 0.5 * (macd_line[idx] + signal_line[idx])

    # small circle at intersection
    ax2.plot(x, y, marker='o', markersize=6, markeredgecolor='green',
             markerfacecolor='white', markeredgewidth=1.5, alpha=1.0)

    # arrow + label
    ax2.annotate(
    "Bullish\nCrossover (Buy)",
    xy=(x, y),
    xytext=(-70, 20),               # 30 points to the right, 15 up
    textcoords='offset points',    # interpret xytext in screen‐points
    ha='left',                     # left‐align the text box
    va='bottom',
    color='green',
    fontsize=8,
    arrowprops=dict(arrowstyle="->", color='green', lw=1.2),
)
    
for idx in sell_signals:
    x = dates[idx]
    y = 0.5 * (macd_line[idx] + signal_line[idx])

    ax2.plot(x, y, marker='o', markersize=6, markeredgecolor='red',
             markerfacecolor='white', markeredgewidth=1.5, alpha=1.0)

    ax2.annotate(
        "Bearish\nCrossover (Sell)",
        xy=(x, y),
        xytext=(25, -5),             # 30 points to the right, 30 down
        textcoords='offset points',   # interpret xytext in points
        ha='left',                    # anchor text left of that xytext point
        va='top',                     # anchor text above that xytext point
        color='red',
        fontsize=8,
        arrowprops=dict(
            arrowstyle="->",
            color='red',
            lw=1.2,
            shrinkA=0,
            shrinkB=2
        )
    )

ax2.set_ylabel('MACD Value', fontsize=12)
ax2.grid(True, alpha=1.0)

for idx in buy_signals:
    ax2.axvline(dates[idx], color='green', linestyle='--', alpha=0.8, linewidth=1)
for idx in sell_signals:
    ax2.axvline(dates[idx], color='red',   linestyle='--', alpha=0.8, linewidth=1)

# Add minor grid for better visibility
ax2.xaxis.set_minor_locator(AutoMinorLocator())
ax2.yaxis.set_minor_locator(AutoMinorLocator())
ax2.grid(which='minor', alpha=0.2)

# Format x-axis to show dates clearly
plt.gcf().autofmt_xdate()

ax2.text(dates[10], 0.3, "Zero Line", color="black", fontsize=8, va="bottom")

# 4) Peak bullish momentum on histogram
imax = np.nanargmax(histogram)
ax2.annotate("Peak\nBullish",
             xy=(dates[imax], histogram[imax]),
             xytext=(dates[imax], histogram[imax] + 1.5),
             ha="center",
             arrowprops=dict(arrowstyle="->", color="darkgreen"),
             color="darkgreen",
             fontsize=8)

# 5) Peak bearish momentum on histogram
imin = np.nanargmin(histogram)
ax2.annotate("Peak\nBearish",
             xy=(dates[imin], histogram[imin]),
             xytext=(dates[imin], histogram[imin] - 1.5),
             ha="center",
             arrowprops=dict(arrowstyle="->", color="darkred"),
             color="darkred",
             fontsize=8)

high       = data['High']
low        = data['Low']
prev_close = data['Close'].shift(1)

tr1 = high - low
tr2 = (high - prev_close).abs()
tr3 = (low  - prev_close).abs()
tr  = pd.concat([tr1, tr2, tr3], axis=1).max(axis=1)


atr_color = "#2ca02c"   # a nice green
N   = 14
# initial SMA for ATR
atr = tr.rolling(window=N).mean().copy()
# Wilder smoothing
for t in range(N, len(atr)):
    atr.iloc[t] = ((N - 1) * atr.iloc[t-1] + tr.iloc[t]) / N

# ─── Plot ATR on ax3 ───
ax3.plot(dates, atr, color='tab:green', linewidth=1)
ax3.set_ylabel('ATR (14-period)', fontsize=10)
ax3.set_xlabel('Date')
ax3.grid(True, alpha=0.3)

# ─── Avg ATR line ───
mean_atr = atr.mean()
ax3.axhline(mean_atr, color='gray', linestyle='--', linewidth=1)
ax3.text(
    dates[int(len(dates)*0.02)], mean_atr * 1.02,
    f'Avg. ATR ({N})',
    color='gray', fontsize=8, va='bottom'
)

# ─── Peak volatility spike ───
imax = atr.idxmax()
line_atr, = ax3.plot(
    dates,
    atr,
    color=atr_color,     # "#2ca02c"
    linewidth=1,
    label=f'ATR Line'
)
ax3.set_ylabel('ATR (14‑period)', fontsize=10)
ax3.set_xlabel('Date')
ax3.grid(True, alpha=0.3)

# ─── Add legend for ATR ───
ax3.legend(
    handles=[line_atr],
    loc='upper left',
    fontsize=8,
    frameon=False
)
ax3.annotate(
    'Volatility\nSpike',
    xy=(imax, atr.loc[imax]),
    xytext=(-50, -20),
    textcoords='offset points',
    ha='center',
    color='red',
    fontsize=8,
    arrowprops=dict(arrowstyle='->', color='red', lw=1)
)

# ─── Calmest period trough ───
imin = atr.idxmin()
ax3.plot(imin, atr.loc[imin], 'o', color='blue', markersize=6)
ax3.annotate(
    'Calmest\nPeriod',
    xy=(imin, atr.loc[imin]),
    xytext=(60, 20),
    textcoords='offset points',
    ha='center',
    va='top',
    color='blue',
    fontsize=8,
    arrowprops=dict(arrowstyle='->', color='blue', lw=1)
)


plt.tight_layout(rect=[0, 0, 1, 0.96])  # Make room for the title
# Save the figure
plt.savefig('aapl_macd_chart.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()