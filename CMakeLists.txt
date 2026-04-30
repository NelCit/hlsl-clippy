cmake_minimum_required(VERSION 3.20)
project(hlsl-clippy LANGUAGES CXX VERSION 0.0.0)

# Compiler floor: MSVC 14.44 (VS 17.14, _MSC_VER == 1944) required for full
# C++23 support (std::expected, std::print, deducing-this, std::flat_map).
if(MSVC AND MSVC_VERSION LESS 1944)
    message(FATAL_ERROR "MSVC 14.44 (VS 17.14) or newer required. Found MSVC ${MSVC_VERSION}.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(hlsl-clippy src/main.cpp)
