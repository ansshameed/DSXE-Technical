#!/bin/bash
# S3 upload script for LOB snapshots and profits data 

# Exit on any error
set -e

# Input parameters with defaults
S3_BUCKET=${1:-"trading-simulation-results"}
S3_PREFIX=${2:-"simulation-$(date +%Y%m%d-%H%M%S)"}
CONFIG_NUM=${3:-"unknown"}

echo "Uploading LOB snapshots and profits to S3..."
echo "Bucket: s3://$S3_BUCKET/$S3_PREFIX/"
echo "Configuration: $CONFIG_NUM"

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
    local target_prefix="$3"
    local dir_type="$4"
    local current_config="$CONFIG_NUM"

    if [ ! -d "$src_dir" ]; then
        echo "$dir_type directory not found at $src_dir, skipping."
        return 0
    fi

    echo "Processing $dir_type files with trial numbers..."
    
    # Count files for progress reporting
    local total_files=$(find "$src_dir" -name "*.csv" | wc -l)
    local processed=0
    local filtered_files=0
    
    # For each file in source directory
    for file in "$src_dir"/*.csv; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            processed=$((processed + 1))
            
            # Create new filename with current configuration
            new_filename="config_${current_config}_trial_unknown_${filename}"
            
            # Copy file to temp directory with new filename
            cp "$file" "$temp_dir/$new_filename"
            filtered_files=$((filtered_files + 1))
            
            # Show progress every 10 files
            if [ $((processed % 10)) -eq 0 ] || [ $processed -eq $total_files ]; then
                echo "  Processed $processed/$total_files $dir_type files"
            fi
        fi
    done
    
    echo "  Selected $filtered_files files for configuration $current_config"
    
    # Check if any files were processed
    if [ "$(ls -A "$temp_dir")" ]; then
        echo "Uploading $dir_type files to S3..."
        # Add retry logic for more robustness
        for attempt in {1..3}; do
            if aws s3 cp "$temp_dir" "s3://$S3_BUCKET/$S3_PREFIX/$target_prefix" --recursive; then
                echo "  Upload of $dir_type files complete!"
                break
            else
                echo "  Upload attempt $attempt failed, retrying in 5 seconds..."
                sleep 5
            fi
            
            if [ $attempt -eq 3 ]; then
                echo "  Warning: Failed to upload $dir_type files after 3 attempts."
            fi
        done
    else
        echo "No $dir_type files found to upload for configuration $current_config."
    fi
}

# Process LOB snapshots
process_and_upload "./lob_snapshots" "$TEMP_LOB_DIR" "lob_snapshots" "LOB snapshot"

# Process profits data
process_and_upload "./profits" "$TEMP_PROFITS_DIR" "profits" "profit"

# Clean up temp directories
rm -rf "$TEMP_LOB_DIR" "$TEMP_PROFITS_DIR" 

# Clear source files after successful upload to prevent duplicates in future uploads
echo "Cleaning up source directories after successful upload..."
rm -f "./lob_snapshots"/*
rm -f "./profits"/*

echo "All uploads complete!"