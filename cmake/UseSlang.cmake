# cmake/UseSlang.cmake
#
# Brings in the Slang compiler library as an imported target `slang::slang`.
#
# Resolution order (highest priority first):
#   (a) Explicit Slang_ROOT (CMake var or env var) pointing at a complete
#       install prefix (include/slang.h + lib/ + bin/). Power-user escape
#       hatch — point this at a custom Slang build (from-source on a fork,
#       a CI artifact, etc.) when the prebuilt does not fit.
#   (b) Per-user prebuilt cache (HLSL_CLIPPY_SLANG_CACHE / env var). The
#       cache is keyed by HLSL_CLIPPY_SLANG_VERSION (see SlangVersion.cmake);
#       bumping the version forces a fresh download and ignores stale cache
#       entries. Default cache root:
#         Windows: %LOCALAPPDATA%/hlsl-clippy/slang/<version>/
#         Linux  : $HOME/.cache/hlsl-clippy/slang/<version>/
#         macOS  : $HOME/.cache/hlsl-clippy/slang/<version>/
#       Populate via tools/fetch-slang.ps1 (Windows) or tools/fetch-slang.sh
#       (POSIX); both download the upstream GitHub release tarball
#       (https://github.com/shader-slang/slang/releases/v<version>).
#
# Both paths produce the SAME public alias target: `slang::slang`. Downstream
# targets (e.g. tools/slang-smoke, core, lsp) link to `slang::slang`
# regardless of which path was selected.
#
# Note: this file used to fall back to a from-source submodule build at
# `external/slang`. The submodule was retired (see commit history + ADR
# 0001 addendum) because every CI run + every fresh worktree paid the
# ~20-minute cold compile, while the upstream prebuilt tarball matches the
# pinned `HLSL_CLIPPY_SLANG_VERSION` exactly. Power users still have the
# Slang_ROOT escape hatch above for custom builds.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/SlangVersion.cmake")

# ---------------------------------------------------------------------------
# hlsl_clippy_deploy_slang_dlls(<target>)
#
# Defined FIRST (before any of the resolution-path early-return blocks below)
# so the helper stays available even when (a) Slang_ROOT hits, (b) the cache
# tier hits, or the re-entrancy guard fires — each of which `return()`s out
# of this file. Previously the helper was defined at the bottom; consumers
# (cli/CMakeLists.txt) hit "Unknown CMake command" on every cache-hit
# regeneration. The body deliberately performs all `TARGET slang::slang`
# checks at call time, so moving the definition above the resolution paths
# is safe.
# ---------------------------------------------------------------------------
function(hlsl_clippy_deploy_slang_dlls target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "hlsl_clippy_deploy_slang_dlls: target '${target}' does not exist."
        )
    endif()

    get_property(_already_done GLOBAL PROPERTY
        _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target})
    if(_already_done)
        return()
    endif()

    if(NOT WIN32)
        set_property(GLOBAL PROPERTY
            _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target} TRUE)
        return()
    endif()

    if(NOT TARGET slang::slang)
        message(FATAL_ERROR
            "hlsl_clippy_deploy_slang_dlls: slang::slang target not defined; "
            "include UseSlang.cmake before calling this helper."
        )
    endif()

    get_target_property(_slang_dll slang IMPORTED_LOCATION)
    if(NOT _slang_dll)
        message(STATUS
            "hlsl_clippy_deploy_slang_dlls(${target}): slang has no "
            "IMPORTED_LOCATION; falling back to TARGET_RUNTIME_DLLS "
            "(submodule build path -- only slang.dll will be copied)."
        )
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS
        )
        set_property(GLOBAL PROPERTY
            _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target} TRUE)
        return()
    endif()

    get_filename_component(_slang_dll_dir "${_slang_dll}" DIRECTORY)

    file(GLOB _slang_runtime_dlls "${_slang_dll_dir}/*.dll")
    if(NOT _slang_runtime_dlls)
        message(WARNING
            "hlsl_clippy_deploy_slang_dlls(${target}): no DLLs found in "
            "'${_slang_dll_dir}'. The build will succeed but the resulting "
            "executable will fail to load Slang at runtime."
        )
        set_property(GLOBAL PROPERTY
            _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target} TRUE)
        return()
    endif()

    foreach(_dll IN LISTS _slang_runtime_dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endforeach()

    set_property(GLOBAL PROPERTY
        _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target} TRUE)
endfunction()

# Re-entrancy guard: tools/slang-smoke/CMakeLists.txt may include this file
# in addition to (a future) top-level include. Bail out cleanly if the
# `slang::slang` target is already defined.
if(TARGET slang::slang)
    return()
endif()

# ---------------------------------------------------------------------------
# Helper: validate that <dir> looks like a Slang install prefix.
#
# Sets <out_var> to TRUE when:
#   <dir>/include/slang.h exists, AND
#   at least one of <dir>/lib/slang.lib, <dir>/lib/libslang.so,
#   <dir>/lib/libslang.dylib exists.
# ---------------------------------------------------------------------------
function(_hlsl_clippy_slang_validate_prefix dir out_var)
    set(${out_var} FALSE PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()
    if(NOT EXISTS "${dir}/include/slang.h")
        return()
    endif()

    set(_lib_candidates
        "${dir}/lib/slang.lib"
        "${dir}/lib/libslang.so"
        "${dir}/lib/libslang.dylib"
        "${dir}/lib/slang.dll"
        "${dir}/bin/slang.dll"
    )
    foreach(_cand IN LISTS _lib_candidates)
        if(EXISTS "${_cand}")
            set(${out_var} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# Helper: import a prebuilt prefix as a SHARED IMPORTED `slang` target with
# its `slang::slang` alias. Mirrors what Slang's own CMakeLists.txt exposes.
# ---------------------------------------------------------------------------
function(_hlsl_clippy_slang_import_prefix dir)
    add_library(slang SHARED IMPORTED GLOBAL)

    set_target_properties(slang PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${dir}/include"
    )

    if(WIN32)
        # On Windows, IMPORTED_LOCATION points at the DLL and IMPORTED_IMPLIB
        # points at the import library. Slang ships both `slang.dll`
        # (forwarder) and `slang-compiler.dll`; we link against `slang.lib`.
        set(_dll "${dir}/bin/slang.dll")
        if(NOT EXISTS "${_dll}")
            set(_dll "${dir}/lib/slang.dll")
        endif()
        set_target_properties(slang PROPERTIES
            IMPORTED_LOCATION "${_dll}"
            IMPORTED_IMPLIB   "${dir}/lib/slang.lib"
        )
    elseif(APPLE)
        set_target_properties(slang PROPERTIES
            IMPORTED_LOCATION "${dir}/lib/libslang.dylib"
        )
    else()
        set_target_properties(slang PROPERTIES
            IMPORTED_LOCATION "${dir}/lib/libslang.so"
        )
    endif()

    add_library(slang::slang ALIAS slang)
endfunction()

# ---------------------------------------------------------------------------
# (a) Explicit Slang_ROOT
# ---------------------------------------------------------------------------
set(_slang_root "")
if(DEFINED Slang_ROOT AND NOT "${Slang_ROOT}" STREQUAL "")
    set(_slang_root "${Slang_ROOT}")
elseif(DEFINED ENV{Slang_ROOT} AND NOT "$ENV{Slang_ROOT}" STREQUAL "")
    set(_slang_root "$ENV{Slang_ROOT}")
endif()

if(NOT "${_slang_root}" STREQUAL "")
    _hlsl_clippy_slang_validate_prefix("${_slang_root}" _is_valid)
    if(_is_valid)
        message(STATUS "hlsl-clippy: using Slang from explicit Slang_ROOT=${_slang_root}")
        _hlsl_clippy_slang_import_prefix("${_slang_root}")
        return()
    else()
        message(WARNING
            "hlsl-clippy: Slang_ROOT='${_slang_root}' does not contain a valid "
            "Slang install (missing include/slang.h or lib/{slang.lib|libslang.so|"
            "libslang.dylib}). Falling through to cache / submodule build."
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# (b) Per-user prebuilt cache
# ---------------------------------------------------------------------------
set(_slang_cache_root "")
if(DEFINED HLSL_CLIPPY_SLANG_CACHE AND NOT "${HLSL_CLIPPY_SLANG_CACHE}" STREQUAL "")
    set(_slang_cache_root "${HLSL_CLIPPY_SLANG_CACHE}")
elseif(DEFINED ENV{HLSL_CLIPPY_SLANG_CACHE} AND NOT "$ENV{HLSL_CLIPPY_SLANG_CACHE}" STREQUAL "")
    set(_slang_cache_root "$ENV{HLSL_CLIPPY_SLANG_CACHE}")
else()
    if(WIN32)
        if(DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
            set(_slang_cache_root "$ENV{LOCALAPPDATA}/hlsl-clippy/slang")
        endif()
    else()
        if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            set(_slang_cache_root "$ENV{HOME}/.cache/hlsl-clippy/slang")
        endif()
    endif()
endif()

if(NOT "${_slang_cache_root}" STREQUAL "")
    set(_slang_cache_dir "${_slang_cache_root}/${HLSL_CLIPPY_SLANG_VERSION}")
    _hlsl_clippy_slang_validate_prefix("${_slang_cache_dir}" _is_valid)
    if(_is_valid)
        message(STATUS "hlsl-clippy: using cached Slang from ${_slang_cache_dir}")
        _hlsl_clippy_slang_import_prefix("${_slang_cache_dir}")
        return()
    endif()
endif()

# ---------------------------------------------------------------------------
# Neither Slang_ROOT nor the prebuilt cache resolved a usable Slang. The
# from-source fallback was retired with the `external/slang` submodule —
# fail hard with a concrete remediation.
# ---------------------------------------------------------------------------
if(WIN32)
    set(_fetch_cmd "pwsh tools/fetch-slang.ps1")
    set(_default_cache "%LOCALAPPDATA%/hlsl-clippy/slang/${HLSL_CLIPPY_SLANG_VERSION}/")
else()
    set(_fetch_cmd "bash tools/fetch-slang.sh")
    set(_default_cache "$HOME/.cache/hlsl-clippy/slang/${HLSL_CLIPPY_SLANG_VERSION}/")
endif()

message(FATAL_ERROR
    "hlsl-clippy: could not locate Slang ${HLSL_CLIPPY_SLANG_VERSION}.\n"
    "\n"
    "  No `Slang_ROOT` is set, and the per-user prebuilt cache at\n"
    "    ${_default_cache}\n"
    "  is empty (or HLSL_CLIPPY_SLANG_CACHE / Slang_ROOT point at an\n"
    "  invalid prefix).\n"
    "\n"
    "  Fix: from the repo root, run\n"
    "    ${_fetch_cmd}\n"
    "  to download the matching prebuilt tarball into the cache, then\n"
    "  re-run cmake.\n"
    "\n"
    "  (Power users: pass `-DSlang_ROOT=<path>` or set the env var to a\n"
    "   custom Slang install prefix to bypass the cache entirely.)"
)

# `hlsl_clippy_deploy_slang_dlls(<target>)` is defined at the top of this
# file. It is unreachable from this branch (FATAL_ERROR aborts), but the
# definition stays valid for the (a) Slang_ROOT and (b) cache paths which
# both `return()` before reaching this fall-through.
