import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns

plt.style.use('default')
sns.set_style("whitegrid")
df = pd.read_csv('results/consolidated_profits.csv')
zip_profits = df['avg_profit_per_trader_1'].values
deeplstm_profits = df['avg_profit_per_trader_2'].values

# Calculate percentage differences
percentage_diff = []

for i in range(len(zip_profits)):
    if zip_profits[i] != 0:  # Avoid division by zero
        diff = ((deeplstm_profits[i] - zip_profits[i]) / zip_profits[i]) * 100
    else:
        diff = 0  
    
    percentage_diff.append({
        'session': i,
        'strategy': 'DEEPLSTM' if diff >= 0 else 'ZIP',
        'percentage_diff': diff if diff >= 0 else -diff,
        'is_positive': diff >= 0
    })

# Convert to DataFrame
plot_df = pd.DataFrame(percentage_diff)
plt.figure(figsize=(10, 6))

# Plot bars with different colors based on which strategy performed better
for idx, row in plot_df.iterrows():
    if row['is_positive']:
        plt.bar(row['session'], row['percentage_diff'], color='navy', width=0.8)
    else:
        plt.bar(row['session'], -row['percentage_diff'], color='crimson', width=0.8)

# Add a horizontal line at 0%
plt.axhline(y=0, color='gray', linestyle='--', alpha=0.7)

# Format y-axis as percentage
plt.gca().yaxis.set_major_formatter(plt.FuncFormatter(lambda x, _: f'{int(x)}%'))

# Add labels and title
plt.xlabel('Number of market session (1 session = 60 seconds)', fontsize=12)
plt.ylabel('Percentage Difference from ZIP Profit', fontsize=12)
plt.title('Percentage profit difference of DEEPLSTM versus ZIP', fontsize=14)

# Add a legend
from matplotlib.patches import Patch
legend_elements = [
    Patch(facecolor='navy', label='DEEPLSTM outperforms'),
    Patch(facecolor='crimson', label='ZIP outperforms')
]
plt.legend(handles=legend_elements, loc='upper left')

# Adjust the layout
plt.tight_layout()

# Save the figure
plt.savefig('./results/percentage_difference.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()

# Print some statistics about the performance comparison
positive_diffs = sum(row['is_positive'] for _, row in plot_df.iterrows())
negative_diffs = len(plot_df) - positive_diffs