cmake_minimum_required(VERSION 3.20)
project(hlsl-clippy LANGUAGES CXX VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /EHsc
    )
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Werror
    )
endif()

add_executable(hlsl-clippy src/main.cpp)
