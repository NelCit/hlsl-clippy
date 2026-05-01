# cmake/UseNlohmannJson.cmake
#
# Brings nlohmann/json in as the imported INTERFACE target
# `nlohmann_json::nlohmann_json`. nlohmann/json is header-only, so there is
# no build/install logic — just an include directory exposed via an
# INTERFACE library.
#
# Mirrors `cmake/UseSlang.cmake`'s shape but is dramatically simpler since
# there is no compiled artifact to track. Re-entrancy guard mirrors
# UseSlang.cmake so multiple inclusions are safe.
#
# Per ADR 0014 §"JSON-RPC layer choice": the LSP frontend (`lsp/`) is the
# only consumer of this dependency. `core/` does not include nlohmann/json.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/NlohmannJsonVersion.cmake")

if(TARGET nlohmann_json::nlohmann_json)
    return()
endif()

set(_nlohmann_json_include_dir
    "${CMAKE_CURRENT_LIST_DIR}/../external/nlohmann_json/include")

if(NOT EXISTS "${_nlohmann_json_include_dir}/nlohmann/json.hpp")
    message(FATAL_ERROR
        "hlsl-clippy: nlohmann/json submodule appears uninitialised. "
        "Expected header at ${_nlohmann_json_include_dir}/nlohmann/json.hpp. "
        "Run `git submodule update --init --recursive`."
    )
endif()

add_library(nlohmann_json_headers INTERFACE)
target_include_directories(nlohmann_json_headers SYSTEM INTERFACE
    "${_nlohmann_json_include_dir}")

add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_headers)

message(STATUS "hlsl-clippy: using nlohmann/json ${HLSL_CLIPPY_NLOHMANN_JSON_VERSION} from ${_nlohmann_json_include_dir}")
