import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

# Set the style
plt.style.use('default')
sns.set_style("whitegrid")

# Use your existing data
df = pd.read_csv('results/consolidated_profits.csv')

# Extract data for the two strategies
zip_profits = df['avg_profit_per_trader_1'].values
deeplstm_profits = df['avg_profit_per_trader_2'].values

# Create a DataFrame for sorting and plotting
plot_data = pd.DataFrame({
    'ZIP': zip_profits,
    'DEEPLSTM': deeplstm_profits
})

# Sort the data by DEEPLSTM profits 
sorted_indices = plot_data['DEEPLSTM'].sort_values().index
sorted_data = plot_data.iloc[sorted_indices].reset_index(drop=True)

# Apply smoothing to ZIP data and use a moving average with window size of 3
window_size = 3
sorted_data['ZIP_smoothed'] = sorted_data['ZIP'].rolling(window=window_size, center=True).mean()
sorted_data['ZIP_smoothed'] = sorted_data['ZIP_smoothed'].fillna(sorted_data['ZIP'])

# Create the plot
plt.figure(figsize=(8, 6))
plt.plot(sorted_data.index, sorted_data['ZIP_smoothed'], color='navy', label='ZIP')
plt.plot(sorted_data.index, sorted_data['DEEPLSTM'], color='crimson', label='DEEPLSTM')

# Add labels and title
plt.xlabel('Market Session Trial Count (1 session = 1 hour)', fontsize=12)
plt.ylabel('Average Profit per Trader per Session', fontsize=12)
plt.title('Raw profit curves of ZIP versus sorted DEEPLSTM', fontsize=14)

plt.legend(loc='upper left')
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('./results/profit_curves_comparison.png', dpi=300, bbox_inches='tight')
plt.show()