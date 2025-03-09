#!/bin/bash

# Configuration
TOTAL_CONFIGS=24643
TRIALS_PER_CONFIG=3

# Smaller batches for better distribution
CONFIGS_PER_BATCH=500
ECR_URI="588738568626.dkr.ecr.eu-west-2.amazonaws.com/dsxe-lob-simulation"

# Ensure output directory exists
mkdir -p kubernetes/jobs

# Calculate number of batches
BATCHES=$(( (TOTAL_CONFIGS + CONFIGS_PER_BATCH - 1) / CONFIGS_PER_BATCH ))
echo "Generating $BATCHES job batch files..."

# Generate job files
for i in $(seq 1 $BATCHES); do
  CONFIG_START=$(( (i-1) * CONFIGS_PER_BATCH ))
  CONFIG_END=$(( i * CONFIGS_PER_BATCH - 1 ))
  
  if [ $CONFIG_END -ge $TOTAL_CONFIGS ]; then
    CONFIG_END=$(( TOTAL_CONFIGS - 1 ))
  fi
  
  cat > kubernetes/jobs/job-batch-$i.yaml << EOF
apiVersion: batch/v1
kind: Job
metadata:
  name: dsxe-simulation-batch-$i
spec:
  parallelism: 1
  completions: 1
  backoffLimit: 4
  template:
    spec:
      containers:
      - name: dsxe-lob-simulation
        image: $ECR_URI:latest
        resources:
          requests:
            memory: "3Gi"
            cpu: "1.5"
          limits:
            memory: "4Gi"
            cpu: "2"
        env:
        - name: CONFIG_START
          value: "$CONFIG_START"
        - name: CONFIG_END
          value: "$CONFIG_END"
        - name: TRIALS_PER_CONFIG
          value: "3"
        - name: AWS_ACCESS_KEY_ID
          valueFrom:
            secretKeyRef:
              name: aws-credentials
              key: aws-access-key-id
        - name: AWS_SECRET_ACCESS_KEY
          valueFrom:
            secretKeyRef:
              name: aws-credentials
              key: aws-secret-access-key
        - name: S3_BUCKET
          value: "dsxe-results"
      restartPolicy: OnFailure
EOF
  
  echo "Created job-batch-$i.yaml for configs $CONFIG_START to $CONFIG_END"
done

echo "Job generation complete!"