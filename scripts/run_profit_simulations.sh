#!/bin/bash

# Configuration parameters
XML_CONFIG="../simulationexample.xml"
SIMULATION_EXECUTABLE="./simulation"
TRIALS=10  # Number of trials to run
RESULTS_DIR="./results"
EXCHANGE_PORT=9999
INJECTOR_PORT=8088
BASE_ORCHESTRATOR_PORT=10001
PROFITS_FILE="$RESULTS_DIR/consolidated_profits.csv"

# Create required directories
mkdir -p "$RESULTS_DIR/logs"
mkdir -p "lob_snapshots" "trades" "market_data" "profits" "messages" "logs" "logs/traders"

# Store the script's own PID to avoid killing itself
MY_PID=$$
echo "Script running with PID: $MY_PID"

# Write CSV header
echo "trial,trader_type_1,num_traders_1,total_profit_1,avg_profit_per_trader_1,trader_type_2,num_traders_2,total_profit_2,avg_profit_per_trader_2" > "$PROFITS_FILE"

# Cleanup function that excludes our own process
cleanup() {
    echo "Cleaning up all processes..."
    # Get PIDs for simulation processes
    SIM_PIDS=$(ps -ef | grep -v grep | grep -v "$MY_PID" | grep "[.]/simulation" | awk '{print $2}' || echo "")
    
    if [ -n "$SIM_PIDS" ]; then
        # First try SIGTERM
        kill $SIM_PIDS 2>/dev/null || true
        sleep 2
        
        # Force kill any remaining
        REMAINING_PIDS=$(ps -ef | grep -v grep | grep -v "$MY_PID" | grep "[.]/simulation" | awk '{print $2}' || echo "")
        if [ -n "$REMAINING_PIDS" ]; then
            kill -9 $REMAINING_PIDS 2>/dev/null || true
        fi
    fi
    sleep 1
    
    # More robust cleanup
    killall -9 simulation 2>/dev/null || true
    sleep 1
    
    # Force release ports
    echo "Forcibly releasing ports..."
    fuser -k 9999/tcp 8088/tcp 10001/tcp 10002/tcp 10003/tcp 10004/tcp 10005/tcp 2>/dev/null || true
    sleep 1
}

# Extract profits from the profit files and consolidate them
extract_profits() {
    local trial_num="$1"
    
    # Find the most recent profit file
    local latest_file=$(ls -t profits/*.csv 2>/dev/null | head -1)
    
    if [ -z "$latest_file" ]; then
        echo "No profit files found!"
        return 1
    fi
    
    # Check if file is empty (only has header)
    if [ $(wc -l < "$latest_file") -le 1 ]; then
        echo "Latest profit file is empty, skipping..."
        return 1
    fi
    
    # Process the profit file for this trial
    echo "Processing profit file: $latest_file"
    
    # Define strategy types in order of config file
    local strategy_types=("zic" "shvr" "vwap" "bb" "macd" "obvd" "obvvwap" "rsi" "rsibb" "zip" "deeplstm" "deepxgb")
    local markets_file="../markets.csv"
    
    # Variables to track active strategies
    local active_strategies=()
    local strategy_counts=()
    
    # Read the markets.csv to get trader counts
    if [ -f "$markets_file" ]; then
        # Read the first line of markets.csv
        IFS=',' read -ra config_values < "$markets_file"
        
        # Find active strategies (those with non-zero counts)
        for i in "${!strategy_types[@]}"; do
            if [[ ${config_values[$i]} -gt 0 ]]; then
                active_strategies+=(${strategy_types[$i]})
                strategy_counts+=($((${config_values[$i]} * 2))) # Multiply by 2 for buyers and sellers
            fi
        done
    else
        echo "Warning: markets.csv not found, cannot determine trader counts"
        return 1
    fi
    
    # Check if we found active strategies
    if [ ${#active_strategies[@]} -eq 0 ]; then
        echo "No active strategies found in config file"
        return 1
    fi
    
    # Initialise profit tracking for active strategies
    local strategy_profits=()
    for i in "${!active_strategies[@]}"; do
        strategy_profits[i]=0
    done
    
    # Process the profit file
    while IFS=, read -r trader_name profit || [ -n "$trader_name" ]; do
        # Skip header line
        if [[ "$trader_name" == "trader_name" || "$trader_name" == *"trader_name"* || "$trader_name" == "agent_name" ]]; then
            continue
        fi
        
        # Trim spaces from trader name
        trader_name=$(echo "$trader_name" | sed 's/^ *//g' | sed 's/ *$//g')
        
        # Match the trader to one of our active strategies
        for i in "${!active_strategies[@]}"; do
            local strategy=${active_strategies[$i]}
            if [[ "$trader_name" == *"$strategy"* ]]; then
                # Add profit to the matching strategy
                strategy_profits[$i]=$(echo "${strategy_profits[$i]} + $profit" | bc)
                break
            fi
        done
    done < "$latest_file"
    
    # Prepare the output line
    local output_line="$trial_num"
    
    # Add data for each active strategy
    for i in "${!active_strategies[@]}"; do
        local strategy=${active_strategies[$i]}
        local count=${strategy_counts[$i]}
        local total_profit=${strategy_profits[$i]}
        local avg_profit=$(echo "scale=2; $total_profit / $count" | bc)
        
        output_line="$output_line,$strategy,$count,$total_profit,$avg_profit"
    done
    
    # Write to consolidated file
    echo "$output_line" >> "$PROFITS_FILE"
    
    echo "Added profit data for trial $trial_num to $PROFITS_FILE"
    
    # Backup this file with trial number to avoid processing it again
    cp "$latest_file" "$latest_file.trial_$trial_num"
}

# Function to run a single trial
run_trial() {
    local trial_num="$1"
    local orchestrator_port=$((BASE_ORCHESTRATOR_PORT + trial_num))
    local log_file="$RESULTS_DIR/logs/trial_${trial_num}.log"
    local exchange_log="$RESULTS_DIR/logs/exchange_trial_${trial_num}.log"
    local injector_log="$RESULTS_DIR/logs/injector_trial_${trial_num}.log"
    
    echo "Running trial $trial_num on port $orchestrator_port"
    
    # Step 1: Start Exchange Node
    echo "Starting Exchange Node on port $EXCHANGE_PORT..."
    $SIMULATION_EXECUTABLE node --port $EXCHANGE_PORT > "$exchange_log" 2>&1 &
    EXCHANGE_PID=$!
    sleep 3  # Give exchange time to initialise
    
    # Check if exchange is running
    if ! ps -p $EXCHANGE_PID > /dev/null; then
        echo "ERROR: Exchange failed to start!"
        echo "Exchange log:"
        cat "$exchange_log"
        return 1
    fi
    
    # Step 2: Start Order Injector Node
    echo "Starting Order Injector Node on port $INJECTOR_PORT..."
    $SIMULATION_EXECUTABLE node --port $INJECTOR_PORT > "$injector_log" 2>&1 &
    INJECTOR_PID=$!
    sleep 3  # Give injector time to initialise
    
    # Check if injector is running
    if ! ps -p $INJECTOR_PID > /dev/null; then
        echo "ERROR: Injector failed to start!"
        echo "Injector log:"
        cat "$injector_log"
        kill $EXCHANGE_PID
        return 1
    fi
    
    # Step 3: Start Orchestrator and monitor its output
    echo "Starting Orchestrator on port $orchestrator_port with XML config..."
    
    # Launch orchestrator in background
    $SIMULATION_EXECUTABLE orchestrator --port $orchestrator_port --config "$XML_CONFIG" > "$log_file" 2>&1 &
    ORCH_PID=$!
    
    # Check if orchestrator started
    sleep 2
    if ! ps -p $ORCH_PID > /dev/null; then
        echo "ERROR: Orchestrator failed to start!"
        echo "Orchestrator log:"
        cat "$log_file"
        kill $EXCHANGE_PID $INJECTOR_PID
        return 1
    fi
    
    # Wait for the trading session to end
    echo "Waiting for trading session to end..."
    local max_wait=100  # Maximum wait time in seconds
    local elapsed=0
    local found_completion=false

    while [ $elapsed -lt $max_wait ]; do
        # Check for profits calculation - this is the last reliable message before hanging
        if grep -q "Profits calculated internally by exchange" "$exchange_log"; then
            sleep 2
            echo "Detected profit calculation - assuming trial is complete."
            found_completion=true
            break
        fi
        
        # Also check for other completion messages
        if grep -q "Finished writing profits to CSV" "$exchange_log" || 
        grep -q "Trading session ended" "$exchange_log"; then
            found_completion=true
            echo "Detected completion message - Trial fully complete."
            break
        fi
        
        # Check if processes are still running
        if ! ps -p $EXCHANGE_PID > /dev/null; then
            echo "WARNING: Exchange process terminated unexpectedly."
            break
        fi
        
        sleep 1
        elapsed=$((elapsed + 1))
        
        # Print progress every 10 seconds
        if [ $((elapsed % 10)) -eq 0 ]; then
            echo "Still waiting... ($elapsed seconds)"
        fi
    done

    # Force termination if we've detected the profit calculation
    if [ "$found_completion" = true ]; then
        echo "Forcing termination of processes to proceed to next trial."
        kill $EXCHANGE_PID $INJECTOR_PID $ORCH_PID 2>/dev/null || true

        sleep 1
        
        # Extract profits and add to consolidated file
        echo "Extracting profits for trial $trial_num..."
        extract_profits "$trial_num"
        
        echo "Trial $trial_num completed successfully"
        return 0
    else
        echo "Trial $trial_num timed out or failed"
        echo "  === Last 20 lines of exchange log ==="
        tail -n 20 "$exchange_log"
        echo "  === Last 20 lines of orchestrator log ==="
        tail -n 20 "$log_file"
        return 1
    fi
}

# Trap signals to ensure cleanup
trap cleanup SIGINT SIGTERM EXIT

# Check if XML config exists
if [ ! -f "$XML_CONFIG" ]; then
    echo "Error: XML configuration file not found at $XML_CONFIG"
    echo "Please provide a valid path to your simulation XML configuration file"
    exit 1
fi

echo "Running $TRIALS trials"
echo "Using XML config: $XML_CONFIG"
echo "Results will be saved to: $PROFITS_FILE"

# Run trials
for TRIAL in $(seq 1 $TRIALS); do
    echo "===== Starting Trial $TRIAL of $TRIALS ====="
    
    # Ensure no previous processes are running
    cleanup
    
    # Run the trial
    run_trial "$TRIAL"
    TRIAL_RESULT=$?
    
    if [ $TRIAL_RESULT -ne 0 ]; then
        echo "WARNING: Trial had issues, but continuing with next trial"
    fi
    
    # Wait between trials
    echo "Waiting 2 seconds before next trial..."
    sleep 2
done

# Final cleanup
cleanup

echo "All trials completed!"
echo "Consolidated profits file created at: $PROFITS_FILE"