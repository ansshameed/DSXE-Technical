import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

# Set the style
plt.style.use('default')
sns.set_style("whitegrid")

# Use your existing data
df = pd.read_csv('../final_profits_1/onetomany_obvvwap_xgb_new.csv')

# Extract data for the two strategies
zip_profits = df['avg_profit_per_trader_1'].values
deeplstm_profits = df['avg_profit_per_trader_2'].values

valid_trials = []
invalid_trials_count = 0

for i in range(len(zip_profits)):
    # Skip zero or negative profits to avoid division issues
    if zip_profits[i] <= 0 or deeplstm_profits[i] <= 0:
        invalid_trials_count += 1
        continue
    
    # Calculate ratio between profits
    ratio = max(zip_profits[i], deeplstm_profits[i]) / min(zip_profits[i], deeplstm_profits[i])
    
    # Keep only trials where ratio is less than 3
    if ratio < 3:
        valid_trials.append(i)
    else:
        invalid_trials_count += 1

# Filter data to include only valid trials
filtered_zip_profits = zip_profits[valid_trials]
filtered_deeplstm_profits = deeplstm_profits[valid_trials]

print(f"Removed {invalid_trials_count} trials with disproportionate profits (3x or more difference)")
print(f"Remaining trials: {len(valid_trials)}")

# Create a DataFrame for sorting and plotting
plot_data = pd.DataFrame({
    'OBVVWAP': filtered_zip_profits,
    'DEEPXGB': filtered_deeplstm_profits
})

# Sort the data by ZIP profits instead of DEEPLSTM profits
sorted_indices = plot_data['OBVVWAP'].sort_values().index
sorted_data = plot_data.iloc[sorted_indices].reset_index(drop=True)

# Apply smoothing to DEEPLSTM data and use a moving average with window size of 8
window_size = 8
sorted_data['DEEPXGB_smoothed'] = sorted_data['DEEPXGB'].rolling(window=window_size, center=True).mean()
sorted_data['DEEPXGB_smoothed'] = sorted_data['DEEPXGB_smoothed'].fillna(sorted_data['DEEPXGB'])

# Create the plot
plt.figure(figsize=(8, 6))
plt.plot(sorted_data.index, sorted_data['DEEPXGB_smoothed'], color='#3399FF', label='BoostTrader')
plt.plot(sorted_data.index, sorted_data['OBVVWAP'], color='#ff7f0e', label='OBVVWAP')

# Add labels and title
plt.xlabel('Market Session Trial Count After Outlier Clean', fontsize=20)
plt.ylabel('Average PPT Per Session', fontsize=20)
plt.xticks(fontsize=20)
plt.yticks(fontsize=20)

plt.legend(loc='upper left', fontsize=18)
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('results_2/OBVVWAP/curve_onetomany_obvvwap_xgb.png', dpi=300, bbox_inches='tight')
plt.show()