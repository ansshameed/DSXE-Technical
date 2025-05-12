import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
from matplotlib.patches import Patch

plt.style.use('default')
sns.set_style("whitegrid")

# Load data
df = pd.read_csv('../final_profits_1/onetomany_obvvwap_xgb_new.csv')
zip_profits = df['avg_profit_per_trader_1'].values
deeplstm_profits = df['avg_profit_per_trader_2'].values

# Filter valid trials
valid_trials = []
invalid_trials_count = 0

for i in range(len(zip_profits)):
    if zip_profits[i] <= 0 or deeplstm_profits[i] <= 0:
        invalid_trials_count += 1
        continue
    ratio = max(zip_profits[i], deeplstm_profits[i]) / min(zip_profits[i], deeplstm_profits[i])
    if ratio < 3:
        valid_trials.append(i)
    else:
        invalid_trials_count += 1

filtered_zip_profits = zip_profits[valid_trials]
filtered_deeplstm_profits = deeplstm_profits[valid_trials]

print(f"Removed {invalid_trials_count} trials with disproportionate profits (3x or more difference)")
print(f"Remaining trials: {len(valid_trials)}")

# Compute percentage differences
percentage_diff = []
for i in range(len(filtered_zip_profits)):
    if filtered_zip_profits[i] != 0:
        diff = ((filtered_deeplstm_profits[i] - filtered_zip_profits[i]) / filtered_zip_profits[i]) * 100
    else:
        diff = 0
    percentage_diff.append({
        'session': i,
        'strategy': 'DeepTrader' if diff >= 0 else 'ZIP',
        'percentage_diff': abs(diff),
        'is_positive': diff >= 0
    })

plot_df = pd.DataFrame(percentage_diff)

# Plot
plt.figure(figsize=(8, 6))
for idx, row in plot_df.iterrows():
    color = '#3399FF' if row['is_positive'] else '#ff7f0e'
    value = row['percentage_diff'] if row['is_positive'] else -row['percentage_diff']
    plt.bar(row['session'], value, color=color, width=1.0, alpha=0.8, edgecolor=color, linewidth=0.1)

plt.axhline(y=0, color='black', linestyle='--', alpha=0.7, linewidth=1.5)
plt.gca().yaxis.set_major_formatter(plt.FuncFormatter(lambda x, _: f'{int(x)}%'))
plt.xlabel('Market Session Trial Count After Outlier Clean', fontsize=18)
plt.ylabel('Relative Profit Advantage of BoostTrader', fontsize=18)

# Legend and stats
legend_elements = [
    Patch(facecolor='#3399FF', label='BoostTrader outperforms'),
    Patch(facecolor='#ff7f0e', label='OBVVWAP outperforms')
]
plt.legend(handles=legend_elements, loc='upper left', fontsize=18)

positive_diffs = sum(row['is_positive'] for _, row in plot_df.iterrows())
negative_diffs = len(plot_df) - positive_diffs

plt.xticks(np.arange(0, len(plot_df), 50))
plt.grid(axis='y', linestyle='--', alpha=0.6)
plt.tight_layout()
plt.yticks(fontsize=18)
plt.xticks(fontsize=18)
plt.savefig('results_2/OBVVWAP/wins_onetomany_obvvwap_xgb.png', dpi=300, bbox_inches='tight')
plt.show()
