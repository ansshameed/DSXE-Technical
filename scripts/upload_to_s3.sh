#!/bin/bash
# S3 upload script for LOB snapshots and profits data 

# Exit on any error
set -e

export AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID}
export AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY}
export AWS_REGION="eu-north-1"

# Access CONFIG values from environment variables
CONFIG_START=${CONFIG_START:-0}
CONFIG_END=${CONFIG_END:-0}
TRIALS_PER_CONFIG=${TRIALS_PER_CONFIG:-3}

# Input parameters with defaults - using a fixed root directory
S3_BUCKET=${1:-"dsxe-results"}
S3_PREFIX="all_simulation_data"
CONFIG_NUM=${3:-"unknown"}

echo "Uploading LOB snapshots and profits to S3..."
echo "Bucket: s3://$S3_BUCKET/$S3_PREFIX/"
echo "Processing configurations from $CONFIG_START to $CONFIG_END"
echo "Each configuration has $TRIALS_PER_CONFIG trials"

# Create temporary directories for renamed files
TEMP_LOB_DIR="./tmp_lob_renamed"
TEMP_PROFITS_DIR="./tmp_profits_renamed"

# Ensure directories exist
mkdir -p "$TEMP_LOB_DIR" "$TEMP_PROFITS_DIR"

# Completely clear temporary directories before processing
rm -rf "$TEMP_LOB_DIR"/*
rm -rf "$TEMP_PROFITS_DIR"/*

# Function to process and upload a directory
process_and_upload() {
    local src_dir="$1"
    local temp_dir="$2"
    local data_type="$3"
    local current_config="$CONFIG_NUM"
    
    if [ ! -d "$src_dir" ]; then
        echo "$data_type directory not found at $src_dir, skipping."
        return 0
    fi
    
    echo "Processing $data_type files..."
    
    # Count files to determine trials per configuration
    local total_files=$(find "$src_dir" -name "*.csv" | wc -l)
    echo "Found $total_files $data_type files"
    
    # Calculate actual configuration number based on CONFIG_START and current_config
    local absolute_config=$((CONFIG_START + current_config - 1))
    echo "Processing absolute configuration: $absolute_config"
    
    # Track trial number
    local trial_count=1
    
    # For each file in source directory
    for file in "$src_dir"/*.csv; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            
            # Create new simpler filename with configuration number and trial number
            new_filename="config_${absolute_config}_trial_${trial_count}_${data_type}.csv"
            
            # Copy file to temp directory with new filename
            cp "$file" "$temp_dir/$new_filename"
            
            # Upload directly to the root structure by data type
            aws s3 cp "$temp_dir/$new_filename" "s3://$S3_BUCKET/$S3_PREFIX/${data_type}/" --region $AWS_REGION
            
            echo "Uploaded $data_type for config $absolute_config, trial $trial_count"
            
            # Increment trial count
            trial_count=$((trial_count + 1))
        fi
    done
    
    echo "Completed uploading $((trial_count-1)) trials for config $absolute_config"
}

# Process LOB snapshots
process_and_upload "./lob_snapshots" "$TEMP_LOB_DIR" "lob_snapshot"

# Process profits data
process_and_upload "./profits" "$TEMP_PROFITS_DIR" "profit"

# Clean up temp directories
rm -rf "$TEMP_LOB_DIR" "$TEMP_PROFITS_DIR"

# Clear source files after successful upload to prevent duplicates in future uploads
echo "Cleaning up source directories after successful upload..."
rm -f "./lob_snapshots"/*
rm -f "./profits"/*

echo "All uploads complete!"