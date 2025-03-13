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
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)
include_directories(
    ${Python3_INCLUDE_DIRS}
    ${Python3_NumPy_INCLUDE_DIRS}
)

# Find ONNX Runtime from Homebrew MAC
set(ONNXRUNTIME_LIB "/opt/homebrew/Cellar/onnxruntime/1.21.0/lib/libonnxruntime.dylib")
set(ONNXRUNTIME_INCLUDE_DIR "/opt/homebrew/Cellar/onnxruntime/1.21.0/include")

if(EXISTS "${ONNXRUNTIME_LIB}" AND IS_DIRECTORY "${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "Found ONNX Runtime:")
    message(STATUS "  - Library: ${ONNXRUNTIME_LIB}")
    message(STATUS "  - Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    include_directories(${ONNXRUNTIME_INCLUDE_DIR})
else()
    message(FATAL_ERROR "ONNX Runtime not found at expected paths.\n  Library: ${ONNXRUNTIME_LIB}\n  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
endif()

# Find JSON library
find_package(nlohmann_json REQUIRED)

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

# Link libraries AFTER defining the targets
if(Boost_FOUND)
    target_link_libraries(simulation ${Boost_LIBRARIES})
endif()

target_link_libraries(simulation 
    ${Python3_LIBRARIES}
    pybind11::embed
    pybind11::module
    nlohmann_json::nlohmann_json
    ${ONNXRUNTIME_LIB}  # Add ONNX Runtime library here
)

configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_configs.cpp ${CMAKE_BINARY_DIR}/generate_configs.cpp COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/run_simulations.sh ${CMAKE_BINARY_DIR}/run_simulations.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/upload_to_s3.sh ${CMAKE_BINARY_DIR}/upload_to_s3.sh COPYONLY)

add_custom_command(
    TARGET simulation POST_BUILD
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/run_simulations.sh
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/upload_to_s3.sh
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

# Check if model files exist and copy them if they do
set(ONNX_MODEL_PATH ${CMAKE_SOURCE_DIR}/src/deeptrader/models/DeepTrader_LSTM/DeepTrader_LSTM.onnx)
set(NORM_VALUES_PATH ${CMAKE_SOURCE_DIR}/src/deeptrader/models/DeepTrader_LSTM/normalization_values.json)

if(EXISTS ${ONNX_MODEL_PATH})
    configure_file(${ONNX_MODEL_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_LSTM/DeepTrader_LSTM.onnx COPYONLY)
    message(STATUS "ONNX model found and copied")
else()
    message(STATUS "ONNX model not found at ${ONNX_MODEL_PATH} - you'll need to run the conversion script first")
endif()

if(EXISTS ${NORM_VALUES_PATH})
    configure_file(${NORM_VALUES_PATH} ${CMAKE_BINARY_DIR}/models/DeepTrader_LSTM/normalization_values.json COPYONLY)
    message(STATUS "Normalization values found and copied")
else()
    message(STATUS "Normalization values not found at ${NORM_VALUES_PATH} - you'll need to run the conversion script first")
endif()