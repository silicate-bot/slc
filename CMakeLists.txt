cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(libslc)

add_library(libslc INTERFACE)

target_include_directories(libslc INTERFACE include)