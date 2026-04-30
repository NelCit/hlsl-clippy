cmake_minimum_required(VERSION 3.20)
project(hlsl-clippy LANGUAGES C CXX VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(hlsl-clippy src/main.cpp)

include(cmake/UseTreeSitter.cmake)
add_subdirectory(tools/treesitter-smoke)
