#!/bin/bash
# S3 upload script for profit experiments data

# Exit on any error
set -e

export AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID}
export AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY}
export AWS_REGION="eu-north-1"
export POD_NAME=${POD_NAME:-$(hostname)}

# Input parameters with defaults
S3_BUCKET=${1:-"dsxe-results"}
TIMESTAMP=${2:-$(date +%Y%m%d-%H%M%S)}
CONFIG_NUM=${3:-"unknown"}
PROFITS_FILE=${4:-""}

# Define S3 prefix - now directly to profit_experiments root
S3_PREFIX="profit_experiments"

echo "Uploading profit experiment data to S3..."
echo "Bucket: s3://$S3_BUCKET/$S3_PREFIX/"
echo "Configuration: $CONFIG_NUM"

# Create temporary directory for renamed files
TEMP_DIR="./tmp_profits_renamed"

# Ensure directory exists
mkdir -p "$TEMP_DIR"

# Completely clear temporary directory before processing
rm -rf "$TEMP_DIR"/*

# Function to upload a specific profits file
upload_specific_file() {
    local src_file="$1"
    local config="$2"
    
    if [ ! -f "$src_file" ]; then
        echo "Profits file not found at $src_file, skipping."
        return 1
    fi
    
    echo "Processing profits file: $src_file"
    
    # Create new filename with configuration number
    local filename=$(basename "$src_file")
    local new_filename="config_${config}_${POD_NAME}_profits.csv"
    
    # Copy file to temp directory with new filename
    cp "$src_file" "$TEMP_DIR/$new_filename"
    
    # Upload to S3 - directly to the profit_experiments directory
    aws s3 cp "$TEMP_DIR/$new_filename" "s3://$S3_BUCKET/$S3_PREFIX/" --region $AWS_REGION
    echo "Uploaded profits for config $config"

    rm -f "$src_file"
    echo "Deleted local file: $src_file"
    
    return 0
}

# Function to process and upload all files in a directory
upload_directory() {
    local src_dir="$1"
    
    if [ ! -d "$src_dir" ]; then
        echo "Directory not found at $src_dir, skipping."
        return 1
    fi
    
    echo "Processing all profit files in $src_dir..."
    
    # Count files
    local total_files=$(find "$src_dir" -name "*.csv" | wc -l)
    echo "Found $total_files profit files"
    
    # Upload each file
    for file in "$src_dir"/*.csv; do
        if [ -f "$file" ]; then
            local filename=$(basename "$file")
            # Extract configuration number from filename if possible
            local config_num=$(echo "$filename" | grep -oP 'config_\K\d+' || echo "unknown")
            
            # Create new filename
            local new_filename="config_${config_num}_${POD_NAME}_profits.csv"
            
            # Copy file to temp directory with new filename
            cp "$file" "$TEMP_DIR/$new_filename"
            
            # Upload to S3 - directly to the profit_experiments directory
            aws s3 cp "$TEMP_DIR/$new_filename" "s3://$S3_BUCKET/$S3_PREFIX/" --region $AWS_REGION
            
            echo "Uploaded $filename as $new_filename"
        fi
    done
    
    echo "Completed uploading all profit files"
    return 0
}

# Determine what to upload based on the parameters
if [ "$CONFIG_NUM" = "all" ]; then
    # Upload all profit files from the directory
    upload_directory "$PROFITS_FILE"
elif [ -n "$PROFITS_FILE" ] && [ -f "$PROFITS_FILE" ]; then
    # Upload a specific profits file
    upload_specific_file "$PROFITS_FILE" "$CONFIG_NUM"
else
    # Check if there are any profit files to upload
    echo "Looking for profit files in ./profits/ directory..."
    if [ -d "./profits" ] && [ "$(ls -A ./profits)" ]; then
        for file in ./profits/*.csv; do
            if [ -f "$file" ]; then
                # Extract configuration number from filename if possible
                config_num=$(echo "$(basename "$file")" | grep -oP 'config_\K\d+' || echo "$CONFIG_NUM")
                
                # Create new filename
                new_filename="config_${config_num}_${POD_NAME}_profits.csv"
                
                # Copy file to temp directory with new filename
                cp "$file" "$TEMP_DIR/$new_filename"
                
                # Upload to S3 - directly to the profit_experiments directory
                aws s3 cp "$TEMP_DIR/$new_filename" "s3://$S3_BUCKET/$S3_PREFIX/" --region $AWS_REGION
                
                echo "Uploaded $(basename "$file") as $new_filename"
            fi
        done
    else
        echo "No profit files found in ./profits/ directory."
    fi
fi

# Clean up temp directory
rm -rf "$TEMP_DIR"

echo "All uploads complete!"