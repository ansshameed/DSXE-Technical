FROM alpine:latest

# Add testing repository for onnxruntime
RUN echo "@testing https://dl-cdn.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories

# Install necessary packages
RUN apk add --no-cache build-base cmake boost-dev aws-cli bash procps findutils \
    python3 py3-pip linux-headers wget

# Try installing onnxruntime with specific repository settings
RUN apk add --no-cache onnxruntime@testing onnxruntime-dev@testing \
    --repository=http://dl-cdn.alpinelinux.org/alpine/edge/testing/ \
    -X http://dl-cdn.alpinelinux.org/alpine/edge/community/ \
    -X http://dl-cdn.alpinelinux.org/alpine/edge/main

# Install nlohmann JSON header
RUN mkdir -p /usr/include/nlohmann && \
    wget https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp -O /usr/include/nlohmann/json.hpp

# Set up project structure
WORKDIR /app

# Copy source files and directory structure
COPY CMakeLists.txt /app/
COPY src/ /app/src/
COPY scripts/ /app/scripts/
COPY simulationexample.xml /app/
COPY IBM-310817.csv /app

# Build the project
RUN cmake -B build && \
    cmake --build build

# Copy the XML config to the build directory for easier access
RUN cp /app/simulationexample.xml /app/build/

# The workaround from GitHub issue - copy the library so it can be found
#https://github.com/microsoft/onnxruntime/issues/2909
RUN find /usr/lib -name "libonnxruntime.so*" -exec cp {} /app/build/ \;

# Set working directory to the build directory
WORKDIR /app/build

# Copy and set up scripts
COPY scripts/run_profit_simulations.sh /app/build/
COPY scripts/upload_profits_to_s3.sh /app/build/
COPY scripts/eks_docker_profit_run.sh /app/build/

# Make scripts executable
RUN chmod +x /app/build/run_profit_simulations.sh /app/build/upload_profits_to_s3.sh /app/build/eks_docker_profit_run.sh && \
    sed -i 's/SIM_PIDS=$(pgrep -f "simulation")/SIM_PIDS=$(ps -ef | grep -v grep | grep -v $$ | grep "[.]\/simulation" | awk "{print \$2}")/' /app/build/run_profit_simulations.sh

# Add debugging output to help diagnose any issues
CMD ["/bin/bash", "-c", "echo 'ONNX Library Location:' && find / -name 'libonnxruntime.so*' 2>/dev/null || echo 'Not found' && env && /bin/bash /app/build/eks_docker_profit_run.sh"]