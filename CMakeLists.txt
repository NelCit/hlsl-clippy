cmake_minimum_required(VERSION 3.20)
project(hlsl-clippy LANGUAGES C CXX VERSION 0.5.2)

# Compiler floor: MSVC 14.44 (VS 17.14, _MSC_VER == 1944) required for full
# C++23 support (std::expected, std::print, deducing-this, std::flat_map).
if(MSVC AND MSVC_VERSION LESS 1944)
    message(FATAL_ERROR "MSVC 14.44 (VS 17.14) or newer required. Found MSVC ${MSVC_VERSION}.")
endif()

set(CMAKE_CXX_STANDARD 23)
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

# Slang must be available before the core library that links it (Phase 3 —
# reflection infrastructure, ADR 0012). Previously only `tools/slang-smoke`
# included this; promote to unconditional so `core` always sees `slang::slang`.
include(cmake/UseSlang.cmake)

# nlohmann/json — JSON-RPC dispatcher dependency for the LSP frontend
# (Phase 5, ADR 0014). Header-only; only `lsp/` consumes it.
include(cmake/UseNlohmannJson.cmake)

# toml++ for `.hlsl-clippy.toml` parsing (per ADR 0008 §5). Header-only;
# fetched in single-include mode so only `core/src/config.cpp` pays the
# compile cost.
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(lsp)

# Smoke tools exercise the third-party integrations (Slang via the prebuilt
# resolved by cmake/UseSlang.cmake, tree-sitter via its submodule). Useful
# for diagnosing the third-party surface but optional for day-to-day rule-
# engine work — default-on with an opt-out flag.
option(HLSL_CLIPPY_BUILD_SLANG_SMOKE "Build the Slang smoke-test tool" ON)
option(HLSL_CLIPPY_BUILD_TREESITTER_SMOKE "Build the tree-sitter smoke-test tool" ON)
if(HLSL_CLIPPY_BUILD_SLANG_SMOKE)
    add_subdirectory(tools/slang-smoke)
endif()
if(HLSL_CLIPPY_BUILD_TREESITTER_SMOKE)
    add_subdirectory(tools/treesitter-smoke)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
