cmake_minimum_required(VERSION 3.16)
project(dis-gateway CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dis-gateway
    src/main.cpp
    src/gateway.cpp
    src/dis/codec.cpp
    src/net/udp_socket.cpp
    src/rules/rule_engine.cpp
    src/rules/field_accessor.cpp
)

target_include_directories(dis-gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(dis-gateway PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(dis-gateway PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

install(TARGETS dis-gateway RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION share/dis-gateway/config)
