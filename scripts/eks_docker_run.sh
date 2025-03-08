#!/bin/bash

# Read configuration range from environment variables (with defaults)
CONFIG_START=${CONFIG_START:-0}
CONFIG_END=${CONFIG_END:-24643}
TRIALS_PER_CONFIG=${TRIALS_PER_CONFIG:-5}

echo "===== CHECKING AWS CREDENTIALS ====="
if [ -z "$AWS_ACCESS_KEY_ID" ] || [ -z "$AWS_SECRET_ACCESS_KEY" ]; then
  echo "Warning: AWS credentials not set. S3 upload may fail."
  echo "Run container with -e AWS_ACCESS_KEY_ID=your_key -e AWS_SECRET_ACCESS_KEY=your_secret"
fi

echo "===== CONFIGURATION ====="
echo "Processing configurations from $CONFIG_START to $CONFIG_END"
echo "Running $TRIALS_PER_CONFIG trials per configuration"

set -e

echo "===== RUNNING GENERATE_CONFIGS ====="
./generate_configs

# Filter markets.csv to only process the specified range
if [ "$CONFIG_START" -ne 0 ] || [ "$CONFIG_END" -ne 24643 ]; then
  echo "Filtering markets.csv to only include configs $CONFIG_START to $CONFIG_END..."
  sed -n "$((CONFIG_START+1)),$((CONFIG_END+1))p" markets.csv > markets_subset.csv
  mv markets_subset.csv markets.csv
  
  # Count total configurations after filtering
  TOTAL_CONFIGS=$(wc -l < markets.csv)
  echo "Processing $TOTAL_CONFIGS configurations from the range"
fi

# Update the TRIALS_PER_CONFIG in run_simulations.sh
if [ "$TRIALS_PER_CONFIG" -ne 5 ]; then
  echo "Setting trials per configuration to $TRIALS_PER_CONFIG..."
  sed -i "s/TRIALS_PER_CONFIG=5/TRIALS_PER_CONFIG=$TRIALS_PER_CONFIG/" ./run_simulations.sh
fi

# Create simulation timestamp based on batch
SIMULATION_TIMESTAMP=$(date +%Y%m%d-%H%M%S)
export SIMULATION_TIMESTAMP="batch_${CONFIG_START}_${CONFIG_END}_${SIMULATION_TIMESTAMP}"
echo "Simulation timestamp: $SIMULATION_TIMESTAMP"

echo "===== RUNNING SIMULATION ====="
bash ./run_simulations.sh