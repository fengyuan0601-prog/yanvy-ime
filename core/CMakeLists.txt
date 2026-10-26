# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.18)
project(ime_core VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(verify_trusted_policy
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/generate_policy.py"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
  COMMENT "Verifying pinned inputs and the compiled trust policy"
  VERBATIM
)
find_path(RIME_INCLUDE_DIR rime_api.h REQUIRED)
find_library(RIME_LIBRARY NAMES rime librime REQUIRED)
add_library(ime_core STATIC ime_core.cpp)
add_dependencies(ime_core verify_trusted_policy)
target_include_directories(ime_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${RIME_INCLUDE_DIR})
target_link_libraries(ime_core PUBLIC ${RIME_LIBRARY})
add_executable(ime-core main.cpp)
target_link_libraries(ime-core PRIVATE ime_core)
if(MSVC)
  target_compile_options(ime_core PRIVATE /W4 /utf-8)
  target_compile_options(ime-core PRIVATE /W4 /utf-8)
endif()
