cmake_minimum_required(VERSION 3.29)
project(proxy C)

set(CMAKE_C_FLAGS "-Wall -Werror -Wextra -pedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_STANDARD 23)
set(BIN_NAME cache-proxy)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
file(GLOB_RECURSE SRC_FILES ${SRC_DIR}/*.c)
add_executable(${BIN_NAME} main.c ${SRC_FILES})
target_link_libraries(${BIN_NAME} pthread)

