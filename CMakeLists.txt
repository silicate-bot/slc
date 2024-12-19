cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(libslc)

add_library(libslc INTERFACE)

target_include_directories(libslc INTERFACE include)

add_executable(libslc_test
  test/main.cpp
)

target_link_libraries(libslc_test PRIVATE libslc)

add_custom_command(
    TARGET libslc_test POST_BUILD
    COMMAND libslc_test
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Running test..."
)
