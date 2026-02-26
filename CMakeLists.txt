# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kk_edge_ai_tflm_hello)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE
  src/tflm_hello_world/constants.c
  src/tflm_hello_world/model.cpp
  src/tflm_hello_world/main_functions.cpp
  src/tflm_hello_world/output_handler.cpp
  src/tflm_hello_world/assert.cpp
)
target_include_directories(app PRIVATE src/tflm_hello_world)

# TFLM hello_world uses local static init; disable thread-safe statics for C++
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)
