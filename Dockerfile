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

# Copy and set up scripts
COPY scripts/run_simulations.sh /app/build/
COPY scripts/upload_to_s3.sh /app/build/
COPY scripts/eks_docker_run.sh /app/build/

# Make scripts executable and modify run_simulations.sh to prevent it from killing itself
RUN chmod +x /app/build/run_simulations.sh /app/build/upload_to_s3.sh /app/build/eks_docker_run.sh && \
    sed -i 's/SIM_PIDS=$(pgrep -f "simulation")/SIM_PIDS=$(ps -ef | grep -v grep | grep -v $$ | grep "[.]\/simulation" | awk "{print \$2}")/' /app/build/run_simulations.sh

# Default command - runs the eks script
CMD ["/bin/bash", "/app/build/eks_docker_run.sh"]