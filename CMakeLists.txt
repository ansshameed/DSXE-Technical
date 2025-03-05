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

if(Boost_FOUND)

    target_link_libraries(simulation ${Boost_LIBRARIES})

endif()

configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_configs.cpp ${CMAKE_BINARY_DIR}/generate_configs.cpp COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/run_simulations.sh ${CMAKE_BINARY_DIR}/run_simulations.sh COPYONLY)

add_custom_command(
    TARGET simulation POST_BUILD
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/run_simulations.sh
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/lob_snapshots)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/trades)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/market_data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/profits)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/messages)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs/traders)
