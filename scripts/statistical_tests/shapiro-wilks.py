import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
import seaborn as sns

# Set some nice plotting defaults
plt.style.use('seaborn-v0_8-whitegrid')
sns.set_palette("deep")
plt.rcParams['figure.figsize'] = [12, 8]
plt.rcParams['figure.dpi'] = 100

# Load the data
#filename = "../../build/results/profits/config_2_profits.csv"
filename = "../final_profits_1/balanced_zic_lstm.csv"

# Read the data
data = []
with open(filename, 'r') as f:
    # Skip header
    next(f)
    for line in f:
        cols = line.strip().split(',')
        data.append({
            'trial': int(cols[0]),
            'trader_type_1': cols[1],
            'num_traders_1': int(cols[2]),
            'total_profit_1': float(cols[3]),
            'avg_profit_per_trader_1': float(cols[4]),
            'trader_type_2': cols[5],
            'num_traders_2': int(cols[6]),
            'total_profit_2': float(cols[7]),
            'avg_profit_per_trader_2': float(cols[8])
        })
df = pd.DataFrame(data)

# Filter out trials with disproportionate profits (3x or more difference)
valid_trials = []
invalid_trials_count = 0

for i, row in df.iterrows():
    profit_1 = row['avg_profit_per_trader_1']
    profit_2 = row['avg_profit_per_trader_2']
    
    # Skip zero or negative profits to avoid division issues
    if profit_1 <= 0 or profit_2 <= 0:
        invalid_trials_count += 1
        continue
    
    # Calculate ratio between profits
    ratio = max(profit_1, profit_2) / min(profit_1, profit_2)
    
    # Keep only trials where ratio is less than 3
    if ratio < 3:
        valid_trials.append(i)
    else:
        invalid_trials_count += 1

# Filter the DataFrame to keep only valid trials
filtered_df = df.iloc[valid_trials]

# Extract the profit columns for analysis
profit_1 = filtered_df['avg_profit_per_trader_1']
profit_2 = filtered_df['avg_profit_per_trader_2']

# Get trader names for better labeling
trader_1 = filtered_df['trader_type_1'].iloc[0]
trader_2 = filtered_df['trader_type_2'].iloc[0]

print(f"Removed {invalid_trials_count} trials with disproportionate profits (3x or more difference)")
print(f"Remaining trials: {len(valid_trials)}")
print(f"Analyzing performance of {trader_1.upper()} vs {trader_2.upper()} (Filtered Data)")
print("-" * 60)

# Basic statistics
print("Basic Statistics:")
print(f"{trader_1.upper()} mean profit: {profit_1.mean():.2f}")
print(f"{trader_1.upper()} median profit: {profit_1.median():.2f}")
print(f"{trader_1.upper()} std dev: {profit_1.std():.2f}")
print(f"{trader_2.upper()} mean profit: {profit_2.mean():.2f}")
print(f"{trader_2.upper()} median profit: {profit_2.median():.2f}")
print(f"{trader_2.upper()} std dev: {profit_2.std():.2f}")
print("-" * 60)

# Create a new DataFrame with just the profits for easier analysis
profits_df = pd.DataFrame({
    trader_1.upper(): profit_1,
    trader_2.upper(): profit_2
})

# Calculate confidence intervals (90%)
def calc_confidence_interval(data, confidence=0.90):
    n = len(data)
    mean = np.mean(data)
    std = np.std(data, ddof=1)
    
    # Calculate the standard error and margin of error
    se = std / np.sqrt(n)
    moe = stats.t.ppf(0.5 + confidence/2, df=n-1) * se
    
    # Calculate the confidence interval
    ci_lower = mean - moe
    ci_upper = mean + moe
    
    return (ci_lower, ci_upper)

# Calculate 90% confidence intervals
ci_1_lower, ci_1_upper = calc_confidence_interval(profit_1)
ci_2_lower, ci_2_upper = calc_confidence_interval(profit_2)

print("90% Confidence Intervals:")
print(f"{trader_1.upper()}: ({ci_1_lower:.2f}, {ci_1_upper:.2f})")
print(f"{trader_2.upper()}: ({ci_2_lower:.2f}, {ci_2_upper:.2f})")

# Check if the confidence intervals overlap
if ci_1_lower > ci_2_upper:
    print(f"Confidence intervals don't overlap: {trader_1.upper()} likely outperforms {trader_2.upper()}")
elif ci_2_lower > ci_1_upper:
    print(f"Confidence intervals don't overlap: {trader_2.upper()} likely outperforms {trader_1.upper()}")
else:
    print("Confidence intervals overlap: No clear winner based on this test")
print("-" * 60)

# Shapiro-Wilk test for normality
print("Shapiro-Wilk Test for Normality:")
shapiro_1 = stats.shapiro(profit_1)
shapiro_2 = stats.shapiro(profit_2)

print(f"{trader_1.upper()}: W={shapiro_1[0]:.4f}, p-value={shapiro_1[1]:.20f}")
print(f"{trader_2.upper()}: W={shapiro_2[0]:.4f}, p-value={shapiro_2[1]:.20f}")

if shapiro_1[1] < 0.05:
    print(f"{trader_1.upper()} profits are NOT normally distributed (p<0.05)")
else:
    print(f"{trader_1.upper()} profits appear to be normally distributed (p≥0.05)")
    
if shapiro_2[1] < 0.05:
    print(f"{trader_2.upper()} profits are NOT normally distributed (p<0.05)")
else:
    print(f"{trader_2.upper()} profits appear to be normally distributed (p≥0.05)")
print("-" * 60)

# Wilcoxon signed-rank test (paired sample test)
wilcoxon_result = stats.wilcoxon(profit_1, profit_2)
print("Wilcoxon Signed-Rank Test:")
print(f"Statistic={wilcoxon_result[0]:.4f}, p-value={wilcoxon_result[1]:.50f}")
p_val = wilcoxon_result[1]
print(f"P-value: ${p_val:.2e}$")


if wilcoxon_result[1] < 0.05:
    if profit_1.mean() > profit_2.mean():
        print(f"Statistically significant difference: {trader_1.upper()} outperforms {trader_2.upper()} (p<0.05)")
    else:
        print(f"Statistically significant difference: {trader_2.upper()} outperforms {trader_1.upper()} (p<0.05)")
else:
    print("No statistically significant difference between the two traders (p≥0.05)")
print("-" * 60)

# Calculate win percentages
wins_1 = sum(profit_1 > profit_2)
win_pct_1 = wins_1 / len(profit_1) * 100
win_pct_2 = 100 - win_pct_1

print(f"{trader_1.upper()} won {wins_1} out of {len(profit_1)} trials ({win_pct_1:.2f}%)")
print(f"{trader_2.upper()} won {len(profit_1) - wins_1} out of {len(profit_1)} trials ({win_pct_2:.2f}%)")
print("-" * 60)

# Try normalization techniques and check if they help with normality
print("\nTesting normalization techniques to see if they help with normality:")

# Min-max normalization
def min_max_normalize(data):
    return (data - data.min()) / (data.max() - data.min())

# Z-score normalization
def z_score_normalize(data):
    return (data - data.mean()) / data.std()

# Apply min-max normalization
profit_1_minmax = min_max_normalize(profit_1)
profit_2_minmax = min_max_normalize(profit_2)

# Apply z-score normalization
profit_1_zscore = z_score_normalize(profit_1)
profit_2_zscore = z_score_normalize(profit_2)

# Test normality after min-max normalization
print("\nShapiro-Wilk Test after Min-Max Normalization:")
shapiro_1_minmax = stats.shapiro(profit_1_minmax)
shapiro_2_minmax = stats.shapiro(profit_2_minmax)
print(f"{trader_1.upper()}: W={shapiro_1_minmax[0]:.20f}, p-value={shapiro_1_minmax[1]:.20f}")
print(f"{trader_2.upper()}: W={shapiro_2_minmax[0]:.20f}, p-value={shapiro_2_minmax[1]:.20f}")

# Test normality after z-score normalization
print("\nShapiro-Wilk Test after Z-Score Normalization:")
shapiro_1_zscore = stats.shapiro(profit_1_zscore)
shapiro_2_zscore = stats.shapiro(profit_2_zscore)
print(f"{trader_1.upper()}: W={shapiro_1_zscore[0]:.20f}, p-value={shapiro_1_zscore[1]:.20f}")
print(f"{trader_2.upper()}: W={shapiro_2_zscore[0]:.20f}, p-value={shapiro_2_zscore[1]:.20f}")

print("\nConclusion on normality after normalization attempts:")
if min(shapiro_1_minmax[1], shapiro_1_zscore[1], shapiro_2_minmax[1], shapiro_2_zscore[1]) < 0.05:
    print("Even after normalization, the data still doesn't follow a normal distribution.")
    print("Using the Wilcoxon signed-rank test (as shown above) is appropriate.")
else:
    print("Normalization has successfully transformed the data to follow a normal distribution.")
    print("Parametric tests like t-tests could be considered.")

print("\nAnalysis complete. Visualizations saved as PNG files.")

print("Hodges-Lehmann Estimator:")

# Calculate all pairwise differences
differences = profit_2 - profit_1

# Calculate the Hodges-Lehmann estimator (median of differences)
hodges_lehmann = np.median(differences)

# Calculate confidence interval for Hodges-Lehmann estimator using bootstrap method
n_bootstrap = 10000
bootstrap_medians = []

for _ in range(n_bootstrap):
    # Resample with replacement
    bootstrap_sample = np.random.choice(differences, size=len(differences), replace=True)
    bootstrap_medians.append(np.median(bootstrap_sample))

# Calculate 95% confidence interval
bootstrap_medians.sort()
lower_ci = bootstrap_medians[int(0.025 * n_bootstrap)]
upper_ci = bootstrap_medians[int(0.975 * n_bootstrap)]

print(f"Hodges-Lehmann estimator: {hodges_lehmann:.20f}")
print(f"95% Confidence Interval: [{lower_ci:.2f}, {upper_ci:.2f}]")

# Interpret the results
if lower_ci > 0:
    print(f"{trader_2.upper()} significantly outperforms {trader_1.upper()} (typical advantage: {hodges_lehmann:.2f} profit units)")
elif upper_ci < 0:
    print(f"{trader_1.upper()} significantly outperforms {trader_2.upper()} (typical advantage: {abs(hodges_lehmann):.2f} profit units)")
else:
    print(f"No significant difference between traders (typical difference: {hodges_lehmann:.2f} profit units)")
