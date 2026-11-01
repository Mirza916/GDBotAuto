cmake_minimum_required(VERSION 3.22.1)
project(gdautobot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared bot core lives at the repository root: <repo>/core
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core)

add_library(gdautobot SHARED
    jni_bridge.cpp
    ${CORE_DIR}/src/common.cpp
    ${CORE_DIR}/src/macro.cpp
    ${CORE_DIR}/src/vision.cpp
    ${CORE_DIR}/src/pilot.cpp
    ${CORE_DIR}/src/engine.cpp)
target_include_directories(gdautobot PRIVATE ${CORE_DIR}/include)
target_compile_options(gdautobot PRIVATE -O2 -Wall)
