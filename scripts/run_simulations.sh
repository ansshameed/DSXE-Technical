#!/bin/bash

# Configuration parameters
MARKETS_FILE="./markets.csv"
XML_CONFIG="../simulationexample.xml"
SIMULATION_EXECUTABLE="./simulation"
TRIALS_PER_CONFIG=5 # Number of trials per configuration
RESULTS_DIR="./results"
EXCHANGE_PORT=9999
INJECTOR_PORT=8088
BASE_ORCHESTRATOR_PORT=10001
TEMP_CONFIG_PATH="./temp_config.csv"

S3_UPLOAD_SCRIPT="./upload_to_s3.sh"
S3_BUCKET="dsxe-results"
SIMULATION_TIMESTAMP=$(date +%Y%m%d-%H%M%S)

# Create required directories
mkdir -p "$RESULTS_DIR/logs"
mkdir -p "lob_snapshots" "trades" "market_data" "profits" "messages" "logs" "logs/traders"
mkdir -p "$(dirname "$TEMP_CONFIG_PATH")"

# Store the script's own PID to avoid killing itself
MY_PID=$$
echo "Script running with PID: $MY_PID"

# Fixed cleanup function that excludes our own process
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

# Function to run a single trial with a specific configuration
run_trial() {
    local config_line="$1"
    local config_num="$2"
    local trial_num="$3"
    local orchestrator_port=$((BASE_ORCHESTRATOR_PORT + trial_num))
    local log_file="$RESULTS_DIR/logs/config_${config_num}_trial_${trial_num}.log"
    local exchange_log="$RESULTS_DIR/logs/exchange_${config_num}_trial_${trial_num}.log"
    local injector_log="$RESULTS_DIR/logs/injector_${config_num}_trial_${trial_num}.log"
    
    echo "Running configuration $config_num, trial $trial_num on port $orchestrator_port"
    
    # Step 1: Create the temp_config.csv file where orchestrator expects it
    echo "$config_line" > "$TEMP_CONFIG_PATH"
    
    # Step 2: Start Exchange Node
    echo "Starting Exchange Node on port $EXCHANGE_PORT..."
    $SIMULATION_EXECUTABLE node --port $EXCHANGE_PORT > "$exchange_log" 2>&1 &
    EXCHANGE_PID=$!
    sleep 3  # Give exchange time to initialize
    
    # Check if exchange is running
    if ! ps -p $EXCHANGE_PID > /dev/null; then
        echo "ERROR: Exchange failed to start!"
        echo "Exchange log:"
        cat "$exchange_log"
        return 1
    fi
    
    # Step 3: Start Order Injector Node
    echo "Starting Order Injector Node on port $INJECTOR_PORT..."
    $SIMULATION_EXECUTABLE node --port $INJECTOR_PORT > "$injector_log" 2>&1 &
    INJECTOR_PID=$!
    sleep 3  # Give injector time to initialize
    
    # Check if injector is running
    if ! ps -p $INJECTOR_PID > /dev/null; then
        echo "ERROR: Injector failed to start!"
        echo "Injector log:"
        cat "$injector_log"
        kill $EXCHANGE_PID
        return 1
    fi
    
    # Step 4: Start Orchestrator and monitor its output
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
    local max_wait=180  # Maximum wait time in seconds
    local elapsed=0
    local found_completion=false

    while [ $elapsed -lt $max_wait ]; do
        # Check for profits calculation - this is the last reliable message before hanging
        if grep -q "Profits calculated internally by exchange" "$exchange_log"; then
            # Wait a few more seconds for any remaining writes
            sleep 2
            echo "Detected profit calculation - assuming trial is complete."
            found_completion=true
            break
        fi
        
        # Also check for other completion messages just in case
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
    fi
    
    # Check if the trial completed successfully
    if [ "$found_completion" = true ]; then
        echo "  Trial $trial_num completed successfully"
        return 0
    else
        echo "  Trial $trial_num timed out or failed"
        echo "  === Last 20 lines of exchange log ==="
        tail -n 20 "$exchange_log"
        echo "  === Last 20 lines of orchestrator log ==="
        tail -n 20 "$log_file"
        return 1
    fi
}

# Trap signals to ensure cleanup
trap cleanup SIGINT SIGTERM EXIT

# Check if markets.csv exists
if [ ! -f "$MARKETS_FILE" ]; then
    echo "Error: Markets file not found at $MARKETS_FILE"
    exit 1
fi

# Check if XML config exists
if [ ! -f "$XML_CONFIG" ]; then
    echo "Error: XML configuration file not found at $XML_CONFIG"
    echo "Please provide a valid path to your simulation XML configuration file"
    exit 1
fi

# Count total configurations
TOTAL_CONFIGS=$(wc -l < "$MARKETS_FILE")
echo "Running $TOTAL_CONFIGS configurations with $TRIALS_PER_CONFIG trials each"
echo "Using XML config: $XML_CONFIG"

# Process each line of the markets.csv file
CONFIG_NUM=1
while IFS=, read -r zic shvr vwap bb macd obvd obvvwap rsi rsibb zip || [ -n "$zic" ]; do
    echo "Starting configuration $CONFIG_NUM/$TOTAL_CONFIGS: $zic,$shvr,$vwap,$bb,$macd,$obvd,$obvvwap,$rsi,$rsibb,$zip"
    
    # Run trials for this configuration
    for TRIAL in $(seq 1 $TRIALS_PER_CONFIG); do
        echo "  Running trial $TRIAL/$TRIALS_PER_CONFIG"
        
        # Ensure no previous processes are running
        cleanup
        
        # Run the trial
        run_trial "$zic,$shvr,$vwap,$bb,$macd,$obvd,$obvvwap,$rsi,$rsibb,$zip" "$CONFIG_NUM" "$TRIAL"
        TRIAL_RESULT=$?
        
        if [ $TRIAL_RESULT -ne 0 ]; then
            echo "WARNING: Trial had issues, but continuing with next trial"
        fi
        
        # Wait between trials
        echo "Waiting 5 seconds before next trial..."
        sleep 5
    done

    #S3 upload script called here
    if [ -f "$S3_UPLOAD_SCRIPT" ]; then
        echo "Uploading data for configuration $CONFIG_NUM to S3..."
        bash "$S3_UPLOAD_SCRIPT" "$S3_BUCKET" "${SIMULATION_TIMESTAMP}/config_${CONFIG_NUM}" "$CONFIG_NUM"
    else
        echo "S3 upload script not found at $S3_UPLOAD_SCRIPT, skipping upload."
    fi
    
    CONFIG_NUM=$((CONFIG_NUM + 1))
done < "$MARKETS_FILE"

# Final cleanup
cleanup
rm -f "$TEMP_CONFIG_PATH"

#Ensure any remaining data is uploaded to S3
if [ -f "$S3_UPLOAD_SCRIPT" ]; then
    echo "Performing final upload of all data to S3..."
    bash "$S3_UPLOAD_SCRIPT" "$S3_BUCKET" "${SIMULATION_TIMESTAMP}/final" "all"
    echo "Upload complete!"
else
    echo "S3 upload script not found at $S3_UPLOAD_SCRIPT, skipping upload."
fi

echo "All configurations completed!"