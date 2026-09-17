cmake_minimum_required(VERSION 4.2)
project(primesolver C)

set(CMAKE_C_STANDARD 11)

add_executable(primesolver main.c
)

target_link_libraries(primesolver m)
