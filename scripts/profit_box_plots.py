import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

plt.style.use('default')
sns.set_theme(style="whitegrid")

# Load the consolidated profits data
df = pd.read_csv('results/consolidated_profits.csv')

plot_data = []

# Process each trial row
for _, row in df.iterrows():
    # Add first strategy data
    plot_data.append({
        'Trader': row['trader_type_1'].upper(),  # Convert to uppercase for display
        'Profit per trader': row['avg_profit_per_trader_1']
    })
    
    # Add second strategy data
    plot_data.append({
        'Trader': row['trader_type_2'].upper(),  # Convert to uppercase for display
        'Profit per trader': row['avg_profit_per_trader_2']
    })

# Convert to DataFrame
plot_df = pd.DataFrame(plot_data)

# Create the box plot
plt.figure(figsize=(8, 6))
ax = sns.boxplot(
    x='Trader', 
    y='Profit per trader', 
    data=plot_df,
    palette=['#8FBCB5', '#F2B1A2']  
)

plt.title('Profit Comparison by Trading Strategy', fontsize=14)
plt.xlabel('Trading Strategy', fontsize=12)
plt.ylabel('Profit per trader', fontsize=12)
plt.tight_layout()

# Save the figure
plt.savefig('./results/profit_comparison_boxplot.png', dpi=300, bbox_inches='tight')

# Show the plot
plt.show()

# Print some statistics
print("Profit Statistics:")
for trader in plot_df['Trader'].unique():
    trader_data = plot_df[plot_df['Trader'] == trader]['Profit per trader']
    print(f"\n{trader}:")
    print(f"  Mean: {trader_data.mean():.2f}")
    print(f"  Median: {trader_data.median():.2f}")
    print(f"  Min: {trader_data.min():.2f}")
    print(f"  Max: {trader_data.max():.2f}")
    print(f"  Standard Deviation: {trader_data.std():.2f}")