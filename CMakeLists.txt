cmake_minimum_required(VERSION 3.20)
project(shader-clippy LANGUAGES C CXX VERSION 0.6.7)

# Compiler floor: MSVC 14.44 (VS 17.14, _MSC_VER == 1944) required for full
# C++23 support (std::expected, std::print, deducing-this, std::flat_map).
if(MSVC AND MSVC_VERSION LESS 1944)
    message(FATAL_ERROR "MSVC 14.44 (VS 17.14) or newer required. Found MSVC ${MSVC_VERSION}.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Interprocedural / link-time optimisation for Release builds. MSVC =>
# `/LTCG`; Clang/GCC => `-flto=thin`. Trims ~25-30% off the shipped CLI
# / LSP binary on Windows in particular, where the rule-pack template
# instantiations tend to bloat unmerged.
#
# Gated to Release (and RelWithDebInfo) so Debug builds stay fast.
# Default-on; opt out with `-DSHADER_CLIPPY_USE_LTO=OFF` if a contributor
# is debugging a codegen issue or hitting an MSVC LTCG ICE.
option(SHADER_CLIPPY_USE_LTO "Enable IPO/LTO for Release / RelWithDebInfo builds" ON)
if(SHADER_CLIPPY_USE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg LANGUAGES CXX)
    if(_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        message(STATUS "shader-clippy: IPO/LTO enabled for Release + RelWithDebInfo")
    else()
        message(STATUS "shader-clippy: IPO/LTO not supported by this toolchain (${_ipo_msg})")
    endif()
endif()

# sccache / ccache compiler launcher (per ADR 0005). When sccache is on
# PATH the configure step picks it up automatically; otherwise this is a
# no-op. CI provisions sccache in `.github/workflows/ci.yml` + `release.yml`.
# Local contributors get the win for free if they run `cargo install
# sccache` (or apt/brew/winget equivalent).
find_program(SHADER_CLIPPY_SCCACHE sccache)
if(SHADER_CLIPPY_SCCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER   "${SHADER_CLIPPY_SCCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SHADER_CLIPPY_SCCACHE}")
    message(STATUS "shader-clippy: using sccache at ${SHADER_CLIPPY_SCCACHE}")
endif()

# CTest opt-in. Defaults to ON for development; pass -DBUILD_TESTING=OFF for
# release builds that don't want to fetch Catch2.
include(CTest)

# Project-wide warning flags as an INTERFACE target. We deliberately do NOT
# apply these via add_compile_options() at the top level, because that would
# propagate /WX -Werror into vendored third-party submodules (Slang, miniz,
# tree-sitter, etc.) and break their builds. Our own targets (core, cli, the
# smoke tools) opt in by linking to shader_clippy_warnings.
add_library(shader_clippy_warnings INTERFACE)

if(MSVC)
    target_compile_options(shader_clippy_warnings INTERFACE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        # /Zc:preprocessor is MSVC cl.exe-only; clang-cl accepts the flag
        # but treats it as a no-op + raises -Wunused-command-line-argument.
        # Gate on `MSVC AND NOT Clang` so we keep the conformant preprocessor
        # on cl.exe and don't break the clang-cl build.
        $<$<NOT:$<CXX_COMPILER_ID:Clang>>:/Zc:preprocessor>
        /utf-8
        /EHsc
    )
    target_compile_definitions(shader_clippy_warnings INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(shader_clippy_warnings INTERFACE
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

# tree-sitter-slang grammar (ADR 0021, sub-phase B.1 — v1.4.0). Builds the
# Slang grammar's parser.c + scanner.c into `tree_sitter::slang` against
# the same `tree_sitter_core` runtime as the HLSL grammar. Parser dispatch
# in `core/src/parser.cpp` selects between the two by `detect_language()`.
include(cmake/UseTreeSitterSlang.cmake)

# Slang must be available before the core library that links it (Phase 3 —
# reflection infrastructure, ADR 0012). Previously only `tools/slang-smoke`
# included this; promote to unconditional so `core` always sees `slang::slang`.
include(cmake/UseSlang.cmake)

# nlohmann/json — JSON-RPC dispatcher dependency for the LSP frontend
# (Phase 5, ADR 0014). Header-only; only `lsp/` consumes it.
include(cmake/UseNlohmannJson.cmake)

# toml++ for `.shader-clippy.toml` parsing (per ADR 0008 §5). Header-only;
# fetched in single-include mode so only `core/src/config.cpp` pays the
# compile cost.
include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    # Pinned to the commit SHA of tag v3.4.0. Git tags are mutable; pinning
    # to the commit SHA prevents an upstream maintainer (or attacker with
    # a stolen token) from re-pointing the v3.4.0 tag at a different tree.
    # Bump procedure: resolve the new commit SHA via
    #   gh api repos/marzer/tomlplusplus/git/ref/tags/v<X.Y.Z>
    # and update both the SHA below and `THIRD_PARTY_LICENSES.md` if the
    # license changed.
    GIT_TAG        30172438cee64926dc41fdd9c11fb3ba5b2ba9de  # v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

add_subdirectory(core)
add_subdirectory(cli)
add_subdirectory(lsp)

# Smoke tools exercise the third-party integrations (Slang via the prebuilt
# resolved by cmake/UseSlang.cmake, tree-sitter via its submodule). Useful
# for diagnosing the third-party surface but optional for day-to-day rule-
# engine work — default-on with an opt-out flag.
option(SHADER_CLIPPY_BUILD_SLANG_SMOKE "Build the Slang smoke-test tool" ON)
option(SHADER_CLIPPY_BUILD_TREESITTER_SMOKE "Build the tree-sitter smoke-test tool" ON)
if(SHADER_CLIPPY_BUILD_SLANG_SMOKE)
    add_subdirectory(tools/slang-smoke)
endif()
if(SHADER_CLIPPY_BUILD_TREESITTER_SMOKE)
    add_subdirectory(tools/treesitter-smoke)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
