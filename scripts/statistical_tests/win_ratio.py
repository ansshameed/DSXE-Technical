import pandas as pd
import numpy as np

def analyze_trading_data(file_path):
    # Read the CSV data
    df = pd.read_csv("../final_profits_1/onetomany_obvvwap_xgb_new.csv")
    
    # Make a copy of the dataframe for analysis
    analysis_df = df.copy()
    
    print("Original dataset shape:", df.shape)
    
    # Check for outliers where one trader's profit is 3x the other
    outliers = ((analysis_df['avg_profit_per_trader_1'] > 3 * analysis_df['avg_profit_per_trader_2']) | 
                (analysis_df['avg_profit_per_trader_2'] > 3 * analysis_df['avg_profit_per_trader_1']))
    
    print(f"Number of outliers found: {outliers.sum()}")
    
    # Remove outliers
    clean_df = analysis_df[~outliers]
    print(f"Clean dataset shape after removing outliers: {clean_df.shape}")
    
    # Calculate wins for each strategy (comparing profits)
    trader1_wins = (clean_df['avg_profit_per_trader_1'] > clean_df['avg_profit_per_trader_2']).sum()
    trader2_wins = (clean_df['avg_profit_per_trader_2'] > clean_df['avg_profit_per_trader_1']).sum()
    ties = (clean_df['avg_profit_per_trader_1'] == clean_df['avg_profit_per_trader_2']).sum()
    
    print(f"\nWins for baseline strategy (trader1/bb): {trader1_wins}")
    print(f"Wins for machine learning strategy (trader2/deeplstm): {trader2_wins}")
    print(f"Ties: {ties}")
    
    # Calculate the difference in the number of wins (trader2 - trader1); the ML trader - baseline strategy
    win_diff = trader2_wins - trader1_wins
    print(f"\nDifference in wins (ML - baseline): {win_diff}")
    
    # Calculate percentage of wins for each trader
    total_non_tie_trials = trader1_wins + trader2_wins
    trader1_win_pct = trader1_wins / total_non_tie_trials * 100
    trader2_win_pct = trader2_wins / total_non_tie_trials * 100
    
    print(f"\nPercentage of wins:")
    print(f"Baseline (trader1/bb): {trader1_win_pct:.2f}%")
    print(f"Machine Learning (trader2/deeplstm): {trader2_win_pct:.2f}%")
    print(f"Win percentage ratio (ML:baseline): {trader2_win_pct:.0f}:{trader1_win_pct:.0f}")
    
    # Calculate ratio of wins (ML wins / baseline wins)
    win_ratio = trader2_wins / trader1_wins
    print(f"\nRatio of wins (ML/baseline): {win_ratio:.2f}")
    
    # Additional analysis: Average profit comparison
    avg_profit_trader1 = clean_df['avg_profit_per_trader_1'].mean()
    avg_profit_trader2 = clean_df['avg_profit_per_trader_2'].mean()
    
    print(f"\nAverage profit per trader:")
    print(f"Baseline (trader1/bb): {avg_profit_trader1:.2f}")
    print(f"Machine Learning (trader2/deeplstm): {avg_profit_trader2:.2f}")
    print(f"Difference (ML - baseline): {avg_profit_trader2 - avg_profit_trader1:.2f}")
    print(f"Ratio (ML/baseline): {avg_profit_trader2 / avg_profit_trader1:.2f}")
    
    # Show examples of outliers if any were removed
    if outliers.sum() > 0:
        print("\nExamples of outliers that were removed:")
        print(df[outliers].head())
    
    # Return key metrics for further analysis if needed, including win percentages
    return {
        'baseline_wins': trader1_wins,
        'ml_wins': trader2_wins,
        'win_difference': win_diff,
        'win_ratio': win_ratio,
        'baseline_avg_profit': avg_profit_trader1,
        'ml_avg_profit': avg_profit_trader2,
        'baseline_win_pct': trader1_win_pct,
        'ml_win_pct': trader2_win_pct
    }

if __name__ == "__main__":
    # Replace with your actual file path if needed
    file_path = "paste.txt"
    metrics = analyze_trading_data(file_path)
    
    # Summary at the end
    print("\nStrategy Wins:")
    print(f"The machine learning (deeplstm) strategy wins {metrics['ml_wins']} times")
    print(f"After removing outliers, the baseline (bb) strategy wins {metrics['baseline_wins']} times\n")

    print("\nMetrics:")
    print(f"The difference in wins (ML - baseline) is {metrics['win_difference']}")
    print(f"Win percentage ratio (ML:baseline): {metrics['ml_win_pct']:.0f}:{metrics['baseline_win_pct']:.0f}")
    print(f"The ratio of ML wins to baseline wins is {metrics['win_ratio']:.2f}")
