cmake_minimum_required(VERSION 3.14)
project(dis-gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dis-gateway
    src/main.cpp
    src/pdu_codec.cpp
    src/rule_engine.cpp
    src/multicast_socket.cpp
    src/gateway.cpp
)

target_include_directories(dis-gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Link pthreads on Linux
find_package(Threads REQUIRED)
target_link_libraries(dis-gateway PRIVATE Threads::Threads)
