cmake_minimum_required(VERSION 3.20)

# Bring in Slang (idempotent if already included by the parent project).
include("${CMAKE_SOURCE_DIR}/cmake/UseSlang.cmake")

add_executable(slang-smoke src/main.cpp)

target_link_libraries(slang-smoke PRIVATE slang::slang)

# Slang public headers come transitively from `slang::slang`'s
# INTERFACE_INCLUDE_DIRECTORIES (set by `_hlsl_clippy_slang_import_prefix`
# in cmake/UseSlang.cmake against the resolved prebuilt prefix). The
# previous explicit `external/slang/include` path was retired with the
# Slang submodule.

# Enforce the project's C++ standard.
set_target_properties(slang-smoke PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

# Apply our own warning flags, separate from Slang's internal ones.
if(MSVC)
    target_compile_options(slang-smoke PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(slang-smoke PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()
