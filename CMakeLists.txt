cmake_minimum_required(VERSION 4.2)
project(primesolver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS -fopenmp)

add_executable(primesolver main.cpp)
