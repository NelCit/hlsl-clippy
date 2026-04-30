# cmake/UseSlang.cmake
#
# Brings in the Slang compiler library as an imported target `slang::slang`.
#
# Slang's own CMakeLists.txt creates a target called `slang` (shared library on
# all platforms).  We disable every optional sub-project we don't need so that
# the build stays as lightweight as possible, then re-export the target under
# the conventional `slang::slang` alias.
#
# Prerequisites documented in external/slang-version.md.

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
# Slang's CMake creates a target named `slang`.  If it ever starts producing
# the alias itself this add_library call is a no-op; if not, we create it.
if(NOT TARGET slang::slang)
    add_library(slang::slang ALIAS slang)
endif()
