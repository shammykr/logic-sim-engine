cmake_minimum_required(VERSION 3.16)
project(logicsim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(logicsim_core
  src/circuit.cpp
  src/verilog_parser.cpp
  src/simulator.cpp
  src/vcd_writer.cpp
  src/stimulus.cpp
)
target_include_directories(logicsim_core PUBLIC include)
target_compile_options(logicsim_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion>)

add_executable(logicsim src/main.cpp)
target_link_libraries(logicsim PRIVATE logicsim_core)
target_compile_options(logicsim PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion>)
