cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(libslc)

add_library(libslc INTERFACE)

target_include_directories(libslc INTERFACE include)

add_executable(slcconv
  src/slcconv.cpp
)

target_link_libraries(slcconv PRIVATE libslc)

