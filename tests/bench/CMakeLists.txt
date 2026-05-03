cmake_minimum_required(VERSION 3.20)

# Performance benchmark target: walks the public 27-shader corpus under
# tests/corpus/ and reports per-file + aggregate lint timings via Catch2's
# built-in BENCHMARK macros (gated on CATCH_CONFIG_ENABLE_BENCHMARKING).
#
# Bench is meaningless in Debug — Catch2's chronometer numbers under -O0
# bear no relation to optimised perf. Skip the target entirely unless the
# active config is Release or RelWithDebInfo. Multi-config generators
# (Visual Studio / Xcode) don't expose CMAKE_BUILD_TYPE meaningfully at
# configure time; in that case we still define the target and let the
# user pick a config at build time — it just won't be wired into a default
# `cmake --build` of a Debug tree.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config)
    if(NOT (CMAKE_BUILD_TYPE STREQUAL "Release"
            OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
        message(STATUS
            "tests/bench: skipping shader_clippy_bench (build type is "
            "'${CMAKE_BUILD_TYPE}'; requires Release or RelWithDebInfo).")
        return()
    endif()
endif()

# Resolve the corpus directory at configure time so the bench binary can
# locate fixtures regardless of cwd. Mirrors the test_config.hpp pattern in
# tests/CMakeLists.txt.
set(SHADER_CLIPPY_CORPUS_DIR "${CMAKE_SOURCE_DIR}/tests/corpus")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/bench_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bench_config.hpp"
    @ONLY
)

add_executable(shader_clippy_bench
    bench_lint.cpp
)

target_include_directories(shader_clippy_bench PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"  # for generated bench_config.hpp
)

target_compile_definitions(shader_clippy_bench PRIVATE
    CATCH_CONFIG_ENABLE_BENCHMARKING
)

target_link_libraries(shader_clippy_bench
    PRIVATE
        shader_clippy_core
        shader_clippy_warnings
        Catch2::Catch2WithMain
)

target_compile_features(shader_clippy_bench PRIVATE cxx_std_23)

set_target_properties(shader_clippy_bench PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Windows: deploy Slang's seven runtime DLLs next to the bench exe so it
# can resolve them at run-time without polluting PATH. The helper is
# defined by cmake/UseSlang.cmake (already included transitively via
# shader_clippy_core); see its docstring for why we can't rely on
# $<TARGET_RUNTIME_DLLS:shader_clippy_bench> alone.
if(WIN32)
    if(COMMAND shader_clippy_deploy_slang_dlls)
        shader_clippy_deploy_slang_dlls(shader_clippy_bench)
    else()
        # Fallback: should not happen because UseSlang.cmake is loaded by
        # the time core is built, but keep a TODO + safe default so the
        # build doesn't hard-fail if the helper ever moves.
        # TODO(bench): remove once shader_clippy_deploy_slang_dlls is the
        # documented public entry point in cmake/UseSlang.cmake.
        add_custom_command(TARGET shader_clippy_bench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:shader_clippy_bench>
                $<TARGET_FILE_DIR:shader_clippy_bench>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

# Intentionally NOT calling catch_discover_tests here: the bench is a
# CLI-driven tool, not part of `ctest`. The nightly bench.yml workflow
# (TODO, see tests/bench/README.md) will invoke the binary directly.
