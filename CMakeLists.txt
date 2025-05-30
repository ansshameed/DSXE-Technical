cmake_minimum_required(VERSION 3.22)
project(simulation)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost 1.76.0 COMPONENTS system filesystem serialization program_options REQUIRED)

if(Boost_FOUND)
    message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")
    message(STATUS "Boost_VERSION: ${Boost_VERSION}")
    include_directories(${Boost_INCLUDE_DIRS})
endif()

# Find Python and pybind11 for DeepTrader
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Flexible ONNX Runtime detection
# Try to find ONNX Runtime using multiple methods
find_path(ONNXRUNTIME_INCLUDE_DIR 
    NAMES onnxruntime_cxx_api.h
    PATHS 
    /usr/include/onnxruntime
    /usr/local/include/onnxruntime
    /opt/homebrew/include/onnxruntime
    ${CUSTOM_ONNXRUNTIME_INCLUDE_DIR}
)

find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime libonnxruntime
    PATHS
    /usr/lib
    /usr/local/lib
    /opt/homebrew/lib
    ${CUSTOM_ONNXRUNTIME_LIB_DIR}
)

# Fallback to environment variable or default Homebrew path if not found
if(NOT ONNXRUNTIME_LIB)
    set(ONNXRUNTIME_LIB "/usr/lib/libonnxruntime.so")
endif()

if(NOT ONNXRUNTIME_INCLUDE_DIR)
    set(ONNXRUNTIME_INCLUDE_DIR "/usr/include/onnxruntime")
endif()

# Verify ONNX Runtime is found
if(EXISTS "${ONNXRUNTIME_LIB}" AND IS_DIRECTORY "${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "Found ONNX Runtime:")
    message(STATUS "  - Library: ${ONNXRUNTIME_LIB}")
    message(STATUS "  - Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    include_directories(${ONNXRUNTIME_INCLUDE_DIR})
else()
    message(FATAL_ERROR "ONNX Runtime not found.\n  Library: ${ONNXRUNTIME_LIB}\n  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
endif()

# Find JSON library
include_directories(/usr/include/nlohmann)

add_executable(simulation src/main.cpp
                          src/networking/tcpserver.cpp
                          src/networking/tcpconnection.cpp
                          src/networking/udpserver.cpp
                          src/networking/networkentity.cpp
                          src/agent/agent.cpp
                          src/agent/traderagent.cpp
                          src/agent/stockexchange.cpp
                          src/order/orderqueue.cpp
                          src/order/orderbook.cpp
                          src/config/configreader.cpp
                          src/pugi/pugixml.cpp)

add_executable(generate_configs scripts/generate_configs.cpp)
add_executable(generate_profit_configs scripts/generate_profit_configs.cpp)

# Link libraries AFTER defining the targets
if(Boost_FOUND)
    target_link_libraries(simulation ${Boost_LIBRARIES})
endif()

target_link_libraries(simulation 
    ${Python3_LIBRARIES}
    ${ONNXRUNTIME_LIB}  # Add ONNX Runtime library here
    dl                  # Dynamic linking library
    pthread             # POSIX threads library
)

configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_configs.cpp ${CMAKE_BINARY_DIR}/generate_configs.cpp COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_profit_configs.cpp ${CMAKE_BINARY_DIR}/generate_profit_configs.cpp COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/run_simulations.sh ${CMAKE_BINARY_DIR}/run_simulations.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/run_profit_simulations.sh ${CMAKE_BINARY_DIR}/run_profit_simulations.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/upload_to_s3.sh ${CMAKE_BINARY_DIR}/upload_to_s3.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/upload_profits_to_s3.sh ${CMAKE_BINARY_DIR}/upload_profits_to_s3.sh COPYONLY)

add_custom_command(
    TARGET simulation POST_BUILD
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/run_simulations.sh
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/run_profit_simulations.sh
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/upload_to_s3.sh
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/upload_profits_to_s3.sh
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/lob_snapshots)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/trades)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/market_data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/profits)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/messages)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs/traders)

# Create models directory in the build directory for ONNX model
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models/DeepTrader_LSTM)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models/DeepTrader_XGB)

# Check if model files exist and copy them if they do
set(LSTM_MODEL_PATH ${CMAKE_SOURCE_DIR}/src/deeptrader/dt_lstm/lstm_models/DeepTrader_LSTM.onnx)
set(XGB_MODEL_PATH ${CMAKE_SOURCE_DIR}/src/deeptrader/dt_xgb/xgb_models/DeepTrader_XGB.onnx)
set(NORM_VALUES_PATH ${CMAKE_SOURCE_DIR}/src/deeptrader/normalised_data/min_max_values.json)

# Copy LSTM model if it exists
if(EXISTS ${LSTM_MODEL_PATH})
    configure_file(${LSTM_MODEL_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_LSTM/DeepTrader_LSTM.onnx COPYONLY)
    message(STATUS "LSTM ONNX model found and copied")
else()
    message(STATUS "LSTM ONNX model not found at ${LSTM_MODEL_PATH}")
endif()

# Copy XGBoost model if it exists
if(EXISTS ${XGB_MODEL_PATH})
    configure_file(${XGB_MODEL_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_XGB/DeepTrader_XGB.onnx COPYONLY)
    message(STATUS "XGBoost ONNX model found and copied")
else()
    message(STATUS "XGBoost ONNX model not found at ${XGB_MODEL_PATH}")
endif()

# Copy shared normalisation values
if(EXISTS ${NORM_VALUES_PATH})
    # Copy to both model directories
    configure_file(${NORM_VALUES_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_LSTM/min_max_values.json COPYONLY)
    configure_file(${NORM_VALUES_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_XGB/min_max_values.json COPYONLY)
    message(STATUS "Normalisation values found and copied")
else()
    message(STATUS "Normalisation values not found at ${NORM_VALUES_PATH}")
endif()