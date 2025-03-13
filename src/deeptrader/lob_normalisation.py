#!/usr/bin/env python3
"""
LOB Snapshot Normaliser for dsxe-results S3 bucket with file limit

Purpose: Normalise LOB snapshot data to [0,1] from the dsxe-results S3 bucket

This script:
1. Connects to the dsxe-results S3 bucket
2. Reads a limited number of LOB snapshots from the all_simulation_data/lob_snapshot path
3. Calculates min/max values for each feature to scale to [0, 1]
4. Normalises the data using min-max normalisation
5. Saves the normalised data and normalisation parameters

Usage:
    python lob_normalisation.py --max_files 100
"""

import os
import argparse
import pickle
import json
import csv
from tqdm import tqdm
import numpy as np
import boto3

def parse_args():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description='Normalise LOB snapshot data from S3')
    parser.add_argument('--bucket', type=str, default='dsxe-results', help='S3 bucket name')
    parser.add_argument('--prefix', type=str, default='all_simulation_data/lob_snapshot', help='S3 prefix path')
    parser.add_argument('--output_dir', type=str, default='normalised_data', help='Output directory')
    parser.add_argument('--sample_size', type=int, default=5000, help='Number of files to sample for min/max calculation')
    parser.add_argument('--region', type=str, default='us-east-1', help='AWS region')
    parser.add_argument('--max_files', type=int, default=5000, help='Maximum number of files to process')
    return parser.parse_args()

def setup_directories(output_dir):
    """Create output directories if they don't exist."""
    os.makedirs(output_dir, exist_ok=True)
    print(f"Output directory: {output_dir}")

def list_s3_files(bucket_name, prefix, region, max_files=None):
    """List CSV files in the S3 bucket under the specified prefix, up to max_files."""
    s3 = boto3.client('s3', region_name=region)
    
    print(f"Listing files in bucket: {bucket_name}/{prefix}")
    
    # Use pagination to handle large buckets and get lists of objects
    file_keys = []
    paginator = s3.get_paginator('list_objects_v2')
    
    for page in paginator.paginate(Bucket=bucket_name, Prefix=prefix):
        if 'Contents' in page:
            for obj in page['Contents']:
                if obj['Key'].endswith('.csv'):
                    file_keys.append(obj['Key'])
                    if max_files and len(file_keys) >= max_files:
                        print(f"Reached maximum file limit ({max_files}), stopping file listing")
                        
                        # Print file names before returning
                        print(f"Found {len(file_keys)} CSV files")
                        print("Files found:")
                        for file_key in file_keys:
                            print(f"  - {file_key}")
                        
                        return file_keys
    
    print(f"Found {len(file_keys)} CSV files")
    print("Files found:")
    for file_key in file_keys:
        print(f"  - {file_key}")
    
    return file_keys

def sample_s3_file(bucket_name, file_key, region, max_rows=5000):
    """Download and sample a file from S3."""
    s3 = boto3.client('s3', region_name=region)
    
    try:
        response = s3.get_object(Bucket=bucket_name, Key=file_key)
        data = response['Body'].read().decode('utf-8')
        
        # Parse CSV content
        rows = []
        for i, line in enumerate(data.splitlines()):
            if i >= max_rows:
                break
            
            if line.strip():
                try:
                    # Try to parse as CSV
                    values = [float(val) for val in line.split(',')]
                    rows.append(values)
                except (ValueError, IndexError):
                    # Skip header or malformed lines
                    continue
        
        return np.array(rows) if rows else np.array([])
    
    except Exception as e:
        print(f"Error sampling file {file_key}: {str(e)}")
        return np.array([])

def calculate_min_max(bucket_name, file_keys, sample_size, region):
    """Calculate global min and max values for each feature across sampled files."""
    print(f"Calculating min/max values across {min(sample_size, len(file_keys))} sample files...")
    
    # Sample a subset of files if needed
    if len(file_keys) > sample_size:
        sampled_keys = np.random.choice(file_keys, size=sample_size, replace=False)
    else:
        sampled_keys = file_keys
    
    all_samples = []
    
    # Process each sampled file
    for file_key in tqdm(sampled_keys, desc="Processing sample files"):
        sample_data = sample_s3_file(bucket_name, file_key, region)
        if sample_data.size > 0:
            all_samples.append(sample_data)
    
    if not all_samples:
        raise ValueError("No valid data found in sample files")
    
    # Combine all samples and calculate min/max
    combined_data = np.vstack(all_samples)
    
    min_values = np.min(combined_data, axis=0)
    max_values = np.max(combined_data, axis=0)
    
    # Print min/max values
    print("\nFeature min/max values:")
    for i, (min_val, max_val) in enumerate(zip(min_values, max_values)):
        print(f"Feature {i}: min={min_val:.6f}, max={max_val:.6f}")
    
    return min_values, max_values

def save_min_max_values(output_dir, min_values, max_values):
    """Save min/max values to files for later use."""
    
    # Save as CSV
    with open(os.path.join(output_dir, 'min_max_values.csv'), 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(min_values)
        writer.writerow(max_values)
    
    # Save as JSON (cleaner alternative)
    with open(os.path.join(output_dir, 'min_max_values.json'), 'w') as f:
        json.dump({
            'min_values': min_values.tolist(),
            'max_values': max_values.tolist()
        }, f, indent=2)
    
    print(f"Min/max values saved to {output_dir}/min_max_values.csv and .json")

def normalise_file(data, min_values, max_values):
    """Apply min-max normalisation to data."""
    normalised = np.copy(data)
    
    for i in range(data.shape[1]):
        # Avoid division by zero
        if max_values[i] > min_values[i]:
            normalised[:, i] = (data[:, i] - min_values[i]) / (max_values[i] - min_values[i])
        else:
            normalised[:, i] = 0.0
    
    return normalised

def process_and_normalise(bucket_name, file_keys, min_values, max_values, output_dir, region, batch_size=10):
    """Process S3 files in batches, normalise and save."""
    s3 = boto3.client('s3', region_name=region)
    output_file = os.path.join(output_dir, 'normalised_data.pkl')
    
    print(f"Processing and normalising {len(file_keys)} files...")
    
    # Process files in batches
    for i in range(0, len(file_keys), batch_size):
        batch_keys = file_keys[i:i+batch_size]
        print(f"Processing batch {i//batch_size + 1}/{(len(file_keys) + batch_size - 1)//batch_size}")
        
        for file_key in tqdm(batch_keys, desc="Normalising files"):
            try:
                # Get file from S3
                response = s3.get_object(Bucket=bucket_name, Key=file_key)
                content = response['Body'].read().decode('utf-8')
                
                # Parse CSV data
                rows = []
                for line in content.splitlines():
                    if line.strip():
                        try:
                            values = [float(val) for val in line.split(',')]
                            rows.append(values)
                        except (ValueError, IndexError):
                            continue
                
                if rows:
                    # Convert to numpy array and normalise
                    data_array = np.array(rows)
                    normalised_data = normalise_file(data_array, min_values, max_values)
                    
                    # Save normalised data
                    with open(output_file, 'ab') as f:
                        pickle.dump(normalised_data, f)
            
            except Exception as e:
                print(f"Error processing {file_key}: {str(e)}")
    
    print(f"Normalised data saved to {output_file}")

def main():
    """Main function."""
    args = parse_args()
    
    # Setup
    setup_directories(args.output_dir)
    
    # List S3 files (limited)
    file_keys = list_s3_files(args.bucket, args.prefix, args.region, args.max_files)
    if not file_keys:
        print("No CSV files found in the bucket path. Please check your bucket name, prefix, and permissions.")
        return
    
    print(f"Using {len(file_keys)} files for normalisation process")
    
    # Calculate min/max values
    min_values, max_values = calculate_min_max(args.bucket, file_keys, min(args.sample_size, len(file_keys)), args.region)
    
    # Save min/max values
    save_min_max_values(args.output_dir, min_values, max_values)
    
    # Process and normalise data
    process_and_normalise(args.bucket, file_keys, min_values, max_values, args.output_dir, args.region)
    
    print("Normalisation complete!")

if __name__ == "__main__":
    main()