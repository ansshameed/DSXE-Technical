FROM alpine:latest

# Install necessary packages including process management tools
RUN apk add --no-cache build-base cmake boost-dev aws-cli bash procps findutils

# Set up project structure
WORKDIR /app

# Copy source files and directory structure
COPY CMakeLists.txt /app/
COPY src/ /app/src/
COPY scripts/ /app/scripts/
COPY simulationexample.xml /app/

# Build the project
RUN cmake -B build && \
    cmake --build build

# Copy the XML config to the build directory for easier access
RUN cp /app/simulationexample.xml /app/build/

# Set working directory to the build directory where you normally run commands
WORKDIR /app/build

# Modify run_simulations.sh to prevent it from killing itself
COPY scripts/run_simulations.sh /app/build/
RUN sed -i 's/SIM_PIDS=$(pgrep -f "simulation")/SIM_PIDS=$(ps -ef | grep -v grep | grep -v $$ | grep "[.]\/simulation" | awk "{print \$2}")/' /app/build/run_simulations.sh && \
    chmod +x /app/build/run_simulations.sh

COPY scripts/upload_to_s3.sh /app/build/
RUN chmod +x /app/build/upload_to_s3.sh
    

# Simple run script for the container
RUN echo '#!/bin/bash' > /app/build/docker_run.sh && \
    echo 'echo "===== CHECKING AWS CREDENTIALS ====="' >> /app/build/docker_run.sh && \
    echo 'if [ -z "$AWS_ACCESS_KEY_ID" ] || [ -z "$AWS_SECRET_ACCESS_KEY" ]; then' >> /app/build/docker_run.sh && \
    echo '  echo "Warning: AWS credentials not set. S3 upload may fail."' >> /app/build/docker_run.sh && \
    echo '  echo "Run container with -e AWS_ACCESS_KEY_ID=your_key -e AWS_SECRET_ACCESS_KEY=your_secret"' >> /app/build/docker_run.sh && \
    echo 'fi' >> /app/build/docker_run.sh && \
    echo 'set -e' >> /app/build/docker_run.sh && \
    echo 'echo "===== RUNNING GENERATE_CONFIGS ====="' >> /app/build/docker_run.sh && \
    echo './generate_configs' >> /app/build/docker_run.sh && \
    echo 'echo "===== RUNNING SIMULATION ====="' >> /app/build/docker_run.sh && \
    echo 'bash ./run_simulations.sh' >> /app/build/docker_run.sh && \
    chmod +x /app/build/docker_run.sh

# Default command - runs the script
CMD ["/bin/bash", "/app/build/docker_run.sh"]