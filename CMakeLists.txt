# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# Ensure out-of-tree STM32U5 DCMI module is loaded (required for st,stm32u5-dcmi)
if(NOT ZEPHYR_EXTRA_MODULES)
  set(ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/modules/stm32u5_dcmi" CACHE STRING "Extra Zephyr modules")
endif()

# Read APP_VERSION from app_version.h and pass to imgtool for MCUboot signing
set(VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/app_version.h")
if(EXISTS "${VERSION_HEADER}")
  file(STRINGS "${VERSION_HEADER}" VERSION_LINES REGEX "#define APP_VERSION")
  foreach(LINE ${VERSION_LINES})
    if(LINE MATCHES "#define APP_VERSION \"([^\"]+)\"")
      set(APP_VERSION "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
endif()
if(NOT DEFINED APP_VERSION)
  set(APP_VERSION "0.0.0")
  message(WARNING "Could not parse APP_VERSION from app_version.h, using 0.0.0")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.conf"
  "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${APP_VERSION}\"\n")
set(OVERLAY_CONFIG ${OVERLAY_CONFIG} "${CMAKE_CURRENT_BINARY_DIR}/version.conf")
message(STATUS "MCUboot image version: ${APP_VERSION} (from app_version.h)")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kk_edge_ai_tflm_hello)

# Generate build version header at build time (ensures Build date updates every build)
set(BUILD_VERSION_H "${CMAKE_CURRENT_BINARY_DIR}/include/generated/build_version.h")
add_custom_target(generate_build_version
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/include/generated"
  COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE="${BUILD_VERSION_H}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_build_version.cmake"
  COMMENT "Generating build version header"
  BYPRODUCTS "${BUILD_VERSION_H}"
)
add_dependencies(app generate_build_version)
target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/include/generated"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE
  src/tflm_hello_world/model_settings.c
  src/tflm_hello_world/person_detect_model_data.c
  src/tflm_hello_world/main_functions.cpp
  src/tflm_hello_world/output_handler.cpp
  src/tflm_hello_world/assert.cpp
)
target_include_directories(app PRIVATE src/tflm_hello_world)

# TFLM hello_world uses local static init; disable thread-safe statics for C++
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)
