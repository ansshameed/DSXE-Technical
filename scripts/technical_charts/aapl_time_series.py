import pandas as pd
import matplotlib.pyplot as plt
import yfinance as yf

# Download Apple stock data for 2024-2025
data = yf.download("AAPL", start="2024-01-01", end="2025-01-01")

# Create the figure and axis
plt.figure(figsize=(12, 4))

# Plot the closing price
plt.plot(data.index, data['Close'], label='AAPL Close Price', color='blue', linewidth=2)

# Add labels and title
plt.title("Apple (AAPL) Stock Price 2024-2025", fontsize=14)
plt.xlabel('Date', fontsize=12)
plt.ylabel('Price ($)', fontsize=12)
plt.legend()

# Format the x-axis to show dates clearly
plt.gcf().autofmt_xdate()

# Find highest and lowest prices
highest_price = data['Close'].max()
lowest_price = data['Close'].min()
highest_date = data['Close'].idxmax()
lowest_date = data['Close'].idxmin()
# Add dots at the high and low points
plt.plot(highest_date, highest_price, 'go', markersize=8)  # Green dot for highest point
plt.plot(lowest_date, lowest_price, 'ro', markersize=8)    # Red dot for lowest point

# Annotate highest point - Remove the scatter point, just use the arrow
plt.annotate(f'Highest: ${float(highest_price):.2f}', 
             xy=(highest_date, highest_price),  # Where the arrow points
             xytext=(-150, 5),  # Offset the text
             textcoords='offset points',
             arrowprops=dict(
                 facecolor='green',
                 shrink=0.05,
                 width=1.5,
                 headwidth=8,  # Make arrow head more visible
                 headlength=8
             ),
             fontsize=10)

# Annotate lowest point - Remove the scatter point, just use the arrow
plt.annotate(f'Lowest: ${float(lowest_price):.2f}', 
             xy=(lowest_date, lowest_price),
             xytext=(80, 20),  # Increase offset to move further away
             textcoords='offset points',
             arrowprops=dict(
                 facecolor='red',
                 shrink=0.05,
                 width=1.5,
                 headwidth=8,
                 headlength=8
             ),
             fontsize=10)

# Add some padding to the y-axis
y_min, y_max = plt.ylim()
plt.ylim(y_min - 5, y_max + 5)

# Improve layout
plt.tight_layout()

plt.savefig('aapl_time_series.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()