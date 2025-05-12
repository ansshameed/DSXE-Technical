#!/bin/bash

# Read configuration range from environment variables (with defaults)
CONFIG_START=${CONFIG_START:-0}
CONFIG_END=${CONFIG_END:-39}  # Default to all 40 configurations
TRIALS_PER_CONFIG=${TRIALS_PER_CONFIG:-500}  # Default to 500 trials per config

echo "===== CHECKING AWS CREDENTIALS ====="
if [ -z "$AWS_ACCESS_KEY_ID" ] || [ -z "$AWS_SECRET_ACCESS_KEY" ]; then
  echo "Warning: AWS credentials not set. S3 upload may fail."
  echo "Run container with -e AWS_ACCESS_KEY_ID=your_key -e AWS_SECRET_ACCESS_KEY=your_secret"
fi

echo "===== CONFIGURATION ====="
echo "Processing profit configurations from $CONFIG_START to $CONFIG_END"
echo "Running $TRIALS_PER_CONFIG trials per configuration"

set -e

echo "===== RUNNING GENERATE_PROFIT_CONFIGS ====="
./generate_profit_configs

# Filter markets_profits.csv to only process the specified range
if [ "$CONFIG_START" -ne 0 ] || [ "$CONFIG_END" -ne 39 ]; then
  echo "Filtering markets_profits.csv to only include configs $CONFIG_START to $CONFIG_END..."
  sed -n "$((CONFIG_START+1)),$((CONFIG_END+1))p" markets_profits.csv > markets_subset.csv
  mv markets_subset.csv markets_profits.csv
  
  # Count total configurations after filtering
  TOTAL_CONFIGS=$(wc -l < markets_profits.csv)
  echo "Processing $TOTAL_CONFIGS profit configurations from the range"
fi

if [ "$TRIALS_PER_CONFIG" -ne 500 ]; then
  echo "Setting trials per configuration to $TRIALS_PER_CONFIG..."
  sed -i "s/TRIALS=500/TRIALS=$TRIALS_PER_CONFIG/" ./run_profit_simulations.sh
fi

# Create simulation timestamp based on batch
SIMULATION_TIMESTAMP=$(date +%Y%m%d-%H%M%S)
export SIMULATION_TIMESTAMP="profit_batch_${CONFIG_START}_${CONFIG_END}_${SIMULATION_TIMESTAMP}"
echo "Profit simulation timestamp: $SIMULATION_TIMESTAMP"

echo "===== RUNNING PROFIT SIMULATION ====="
bash ./run_profit_simulations.sh