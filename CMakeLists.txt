cmake_minimum_required(VERSION 3.20)
project(hlsl-clippy LANGUAGES C CXX VERSION 0.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CTest opt-in. Defaults to ON for development; pass -DBUILD_TESTING=OFF for
# release builds that don't want to fetch Catch2.
include(CTest)

# Project-wide warning flags as an INTERFACE target. We deliberately do NOT
# apply these via add_compile_options() at the top level, because that would
# propagate /WX -Werror into vendored third-party submodules (Slang, miniz,
# tree-sitter, etc.) and break their builds. Our own targets (core, cli, the
# smoke tools) opt in by linking to hlsl_clippy_warnings.
add_library(hlsl_clippy_warnings INTERFACE)

if(MSVC)
    target_compile_options(hlsl_clippy_warnings INTERFACE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /EHsc
    )
    target_compile_definitions(hlsl_clippy_warnings INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(hlsl_clippy_warnings INTERFACE
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

# tree-sitter must be available before the core library that links it.
include(cmake/UseTreeSitter.cmake)

add_subdirectory(core)
add_subdirectory(cli)

add_subdirectory(tools/slang-smoke)
add_subdirectory(tools/treesitter-smoke)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
