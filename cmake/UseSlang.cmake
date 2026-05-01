# cmake/UseSlang.cmake
#
# Brings in the Slang compiler library as an imported target `slang::slang`.
#
# Resolution order (highest priority first):
#   (a) Explicit Slang_ROOT (CMake var or env var) pointing at a complete
#       install prefix (include/slang.h + lib/ + bin/). Used as-is; the
#       submodule is NOT configured.
#   (b) Per-user prebuilt cache (HLSL_CLIPPY_SLANG_CACHE / env var). The
#       cache is keyed by HLSL_CLIPPY_SLANG_VERSION (see SlangVersion.cmake),
#       so a submodule SHA bump invalidates stale cache entries on its own.
#       Default cache root:
#         Windows: %LOCALAPPDATA%/hlsl-clippy/slang/<version>/
#         Linux  : $HOME/.cache/hlsl-clippy/slang/<version>/
#       Populate via tools/fetch-slang.ps1 (Windows) or tools/fetch-slang.sh
#       (Linux); both download the upstream GitHub release tarball.
#   (c) Fallback: build the vendored submodule from source (the historical
#       behaviour). No environment changes required — this remains the
#       default when neither (a) nor (b) is present.
#
# All three paths produce the SAME public alias target: `slang::slang`.
# Downstream targets (e.g. tools/slang-smoke) link to `slang::slang`
# regardless of which path was selected.
#
# Prerequisites for the from-source path documented in
# external/slang-version.md.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/SlangVersion.cmake")

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
# (c) Fallback: vendored submodule build (historical behaviour)
# ---------------------------------------------------------------------------
message(STATUS
    "hlsl-clippy: building Slang ${HLSL_CLIPPY_SLANG_VERSION} from vendored "
    "submodule (no Slang_ROOT and no cache hit at "
    "${_slang_cache_root}/${HLSL_CLIPPY_SLANG_VERSION}). "
    "Run tools/fetch-slang.ps1 (Windows) or tools/fetch-slang.sh (Linux) to "
    "populate the cache and avoid this rebuild on subsequent worktrees."
)

# ---- Slang build knobs ---------------------------------------------------
# Disable everything we don't need.
set(SLANG_ENABLE_TESTS     OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_GFX       OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGD    OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGRT   OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_REPLAYER  OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGI    OFF CACHE BOOL "" FORCE)

# ---- Bring in Slang -------------------------------------------------------
add_subdirectory(
    "${CMAKE_SOURCE_DIR}/external/slang"
    "${CMAKE_BINARY_DIR}/external/slang"
    EXCLUDE_FROM_ALL
)

# ---- Re-export as slang::slang --------------------------------------------
# Slang's CMake creates a target named `slang`. If it ever starts producing
# the alias itself this add_library call is a no-op; if not, we create it.
if(NOT TARGET slang::slang)
    add_library(slang::slang ALIAS slang)
endif()

# ---------------------------------------------------------------------------
# hlsl_clippy_deploy_slang_dlls(<target>)
#
# Attaches a POST_BUILD step to <target> that copies every Slang runtime
# DLL into $<TARGET_FILE_DIR:${target}> on Windows. No-op on Linux/macOS
# (RPATH handles loader resolution there).
#
# Why a custom helper instead of `$<TARGET_RUNTIME_DLLS:${target}>`?
#   `$<TARGET_RUNTIME_DLLS>` only walks the *direct* IMPORTED_LOCATION of
#   each linked SHARED IMPORTED target. Our `slang::slang` import points at
#   `slang.dll`, but the Slang runtime is actually a constellation of seven
#   DLLs that `slang.dll` dlopen()s at compile time:
#       slang.dll
#       slang-compiler.dll
#       slang-glsl-module.dll
#       slang-glslang.dll
#       slang-llvm.dll
#       slang-rt.dll
#       gfx.dll
#   None of those (except `slang.dll`) appear in the generator-expression
#   walk, so the previous per-target inline `copy_if_different` blocks were
#   silently shipping an under-populated bin/ directory. Any rule that
#   exercised reflection / IR codegen would then fail at runtime with a
#   "module not found" error from Slang's loader.
#
# Implementation: resolve the Slang import target's IMPORTED_LOCATION to its
# containing directory, glob `*.dll` in that directory at configure time,
# and emit one `copy_if_different` per glob hit. The cache-tier resolution
# (path (b) above) places all seven DLLs in `<cache>/<version>/bin/`, so the
# glob captures them in one pass.
#
# Idempotent — re-invocation with the same target is a no-op.
# ---------------------------------------------------------------------------
function(hlsl_clippy_deploy_slang_dlls target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "hlsl_clippy_deploy_slang_dlls: target '${target}' does not exist."
        )
    endif()

    # Idempotency guard: bail out if we've already attached the POST_BUILD
    # step to this target. Uses a directory-scoped property so repeated
    # includes of the calling CMakeLists don't double-attach.
    get_property(_already_done GLOBAL PROPERTY
        _HLSL_CLIPPY_SLANG_DLLS_ALREADY_DEPLOYED_${target})
    if(_already_done)
        return()
    endif()

    # No-op on non-Windows: ELF/Mach-O loaders use RPATH, which Slang's own
    # CMake (and our cache layout) wires up correctly.
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

    # Resolve the imported DLL location → directory. `slang::slang` is an
    # ALIAS; query the underlying `slang` target.
    get_target_property(_slang_dll slang IMPORTED_LOCATION)
    if(NOT _slang_dll)
        # Some build paths (the from-source submodule fallback) produce a
        # non-imported `slang` target whose runtime location is only known
        # at build time. In that case fall through with a generator-
        # expression based copy of TARGET_RUNTIME_DLLS — which at least
        # picks up `slang.dll` itself; the cache tier (the only path
        # validated locally per the task spec) always populates
        # IMPORTED_LOCATION.
        message(STATUS
            "hlsl_clippy_deploy_slang_dlls(${target}): slang has no "
            "IMPORTED_LOCATION; falling back to TARGET_RUNTIME_DLLS "
            "(submodule build path — only slang.dll will be copied)."
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

    # Glob ALL DLLs in Slang's bin/ at configure time. This intentionally
    # captures the 7 transitive runtime DLLs (slang.dll, slang-compiler.dll,
    # slang-glsl-module.dll, slang-glslang.dll, slang-llvm.dll, slang-rt.dll,
    # gfx.dll) plus any future siblings — `$<TARGET_RUNTIME_DLLS>` would
    # only catch slang.dll because the rest are dlopen()ed, not linked.
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
