FROM ubuntu:22.04 AS builder

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    git \
    python3 \
    python3-pip \
    ca-certificates

RUN wget https://sourceforge.net/projects/boost/files/boost/1.76.0/boost_1_76_0.tar.gz && \
    tar -xzf boost_1_76_0.tar.gz && \
    cd boost_1_76_0 && \
    ./bootstrap.sh --prefix=/usr && \
    ./b2 install \
        --with-system \
        --with-filesystem \
        --with-serialization \
        --with-program_options \
        --layout=system && \  
    cd .. && \
    rm -rf boost_1_76_0.tar.gz boost_1_76_0


# Install ONNX Runtime manually 
RUN wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-linux-x64-1.16.3.tgz && \
    tar -xzf onnxruntime-linux-x64-1.16.3.tgz && \
    mkdir -p /onnxruntime-lib

# Copy ONNX Runtime libraries and headers
RUN cp -r onnxruntime-linux-x64-1.16.3/include /onnxruntime-lib/ && \
    cp -r onnxruntime-linux-x64-1.16.3/lib /onnxruntime-lib/

# Install nlohmann JSON header
RUN mkdir -p /usr/include/nlohmann && \
    wget https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp -O /usr/include/nlohmann/json.hpp

# Stage 2: Final Ubuntu Image
FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Add libary path and root configurations
ENV BOOST_ROOT=/usr

# Install runtime dependencies - install specific Boost components
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    python3-pip \
    python3 \
    python3-venv \
    python3-dev \
    bash \
    procps \
    psmisc \
    ca-certificates \
    build-essential \
    cmake \
    libssl-dev \       
    libpthread-stubs0-dev \
    && pip3 install awscli \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/lib/libboost_* /usr/lib/
COPY --from=builder /usr/include/boost /usr/include/boost/
COPY --from=builder /onnxruntime-lib/lib/libonnxruntime.so* /usr/lib/
COPY --from=builder /onnxruntime-lib/include /usr/include/onnxruntime
COPY --from=builder /usr/include/nlohmann /usr/include/nlohmann

RUN ldconfig

WORKDIR /app

# Set up project structure
COPY CMakeLists.txt /app/
COPY src/ /app/src/
COPY scripts/ /app/scripts/
COPY simulationexample.xml /app/

# Build the project 
RUN cmake -B build && \
    cmake --build build

# Copy the XML config to the build directory for easier access
RUN cp /app/simulationexample.xml /app/build/

# Set working directory to the build directory
WORKDIR /app/build

# Copy and set up scripts
COPY scripts/run_profit_simulations.sh /app/build/
COPY scripts/upload_profits_to_s3.sh /app/build/
COPY scripts/eks_docker_profit_run.sh /app/build/

# Make scripts executable and modify run_simulations.sh 
RUN chmod +x /app/build/run_profit_simulations.sh /app/build/upload_profits_to_s3.sh /app/build/eks_docker_profit_run.sh && \
    sed -i 's/SIM_PIDS=$(pgrep -f "simulation")/SIM_PIDS=$(ps -ef | grep -v grep | grep -v $$ | grep "[.]\/simulation" | awk "{print \$2}")/' /app/build/run_profit_simulations.sh

# Default command - runs the eks script
CMD ["/bin/bash", "-c", "env; /bin/bash /app/build/eks_docker_profit_run.sh"]