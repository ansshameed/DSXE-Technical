import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

plt.style.use('default')
sns.set_theme(style="whitegrid")

# Load the consolidated profits data
df = pd.read_csv('../final_profits_1/onetomany_obvvwap_xgb_new.csv')

# Check what's actually in the data
print("Trader types in data:")
print("Trader 1 types:", df['trader_type_1'].unique())
print("Trader 2 types:", df['trader_type_2'].unique())

# Filter out trials with disproportionate profits
valid_trials = []
invalid_trials_count = 0

for i, row in df.iterrows():
    shvr_profit = row['avg_profit_per_trader_1']
    deeplstm_profit = row['avg_profit_per_trader_2']
    
    # Skip zero or negative profits
    if shvr_profit <= 0 or deeplstm_profit <= 0:
        invalid_trials_count += 1
        continue
    
    # Calculate ratio
    ratio = max(shvr_profit, deeplstm_profit) / min(shvr_profit, deeplstm_profit)
    
    # Keep only trials where ratio is less than 3
    if ratio < 3:
        valid_trials.append(i)
    else:
        invalid_trials_count += 1

# Filter the DataFrame
filtered_df = df.iloc[valid_trials]

print(f"Removed {invalid_trials_count} trials with disproportionate profits")
print(f"Remaining trials: {len(valid_trials)}")

plot_data = []

# Process each filtered trial row - standardizing names
for _, row in filtered_df.iterrows():
    # Add Shaver data
    plot_data.append({
        'Trader': 'OBVVWAP',  # Consistent naming
        'Average Profit Per Trader': row['avg_profit_per_trader_1']
    })
    # Add DeepTrader data
    plot_data.append({
        'Trader': 'BoostTrader',  # Consistent naming
        'Average Profit Per Trader': row['avg_profit_per_trader_2']
    })

# Convert to DataFrame
plot_df = pd.DataFrame(plot_data)

# Ensure consistent ordering
plot_df['Trader'] = pd.Categorical(plot_df['Trader'], 
                                  categories=['BoostTrader', 'OBVVWAP'], 
                                  ordered=True)

palette = {'BoostTrader': '#3399FF', 'OBVVWAP': '#ff7f0e'} 

# Create the box plot
plt.figure(figsize=(8, 6))
ax = sns.boxplot(
    x='Trader', 
    y='Average Profit Per Trader', 
    data=plot_df,
    palette=palette,
    flierprops=dict(marker='o', markerfacecolor='red', markersize=6, linestyle='none')
)

ax.set_xticklabels(ax.get_xticklabels(), fontsize=20)

plt.xlabel('Strategy', fontsize=20)
plt.ylabel('Average PPT', fontsize=20)
plt.yticks(fontsize=20)
plt.tight_layout()

# Save the figure
plt.savefig('results_2/OBVVWAP/box_onetomany_obvvwap_xgb.png', dpi=300, bbox_inches='tight')
plt.show()

# Print statistics correctly
print("Profit Statistics (Filtered Data):")
for trader in plot_df['Trader'].unique():
    trader_data = plot_df[plot_df['Trader'] == trader]['Average Profit Per Trader']
    print(f"\n{trader}:")
    print(f"  Mean: {trader_data.mean():.2f}")
    print(f"  Median: {trader_data.median():.2f}")
    print(f"  Min: {trader_data.min():.2f}")
    print(f"  Max: {trader_data.max():.2f}")
    print(f"  Standard Deviation: {trader_data.std():.2f}")