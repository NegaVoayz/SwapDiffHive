cmake_minimum_required(VERSION 3.30)

project(SwapDiffHive VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(SwapDiffHive STATIC src/FreeChain.cc)
target_include_directories(SwapDiffHive PUBLIC ${PROJECT_SOURCE_DIR}/include)