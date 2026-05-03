cmake_minimum_required(VERSION 3.20)

# Resolve project root at configure time so the binary knows where fixtures are.
set(SHADER_CLIPPY_ROOT "${CMAKE_SOURCE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.hpp"
    @ONLY
)

add_executable(treesitter-smoke
    main.cpp
)

target_include_directories(treesitter-smoke
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"   # for generated config.hpp
)

set_target_properties(treesitter-smoke PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(treesitter-smoke
    PRIVATE
        tree_sitter::hlsl
        tree_sitter::tree_sitter
)

# Compiler-specific warning flags (matching project standards).
# /EHsc enables standard C++ exception unwinding semantics; without it,
# MSVC raises C4530 from <chrono> / <ostream> templates, which /WX
# promotes to an error.
if(MSVC)
    target_compile_options(treesitter-smoke PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(treesitter-smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
