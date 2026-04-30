# cmake/UseTreeSitter.cmake
#
# Defines two imported static-library targets built from vendored sources:
#   tree_sitter::tree_sitter  - the core runtime
#   tree_sitter::hlsl         - the HLSL grammar
#
# Both targets are built from the submodules in external/.

cmake_minimum_required(VERSION 3.20)

# ── tree_sitter::tree_sitter ────────────────────────────────────────────────

add_library(tree_sitter_core STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/src/lib.c"
)

# lib.c is a unity build that #includes all other .c files internally.
# It is C99 code, so compile it as C (not CXX).
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/src/lib.c"
    PROPERTIES LANGUAGE C
)

target_include_directories(tree_sitter_core
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/include"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/src"
)

# Suppress warnings from third-party C99 sources on all compilers.
if(MSVC)
    target_compile_options(tree_sitter_core PRIVATE
        /W0
        /wd4100 /wd4200 /wd4244 /wd4245 /wd4267
    )
else()
    target_compile_options(tree_sitter_core PRIVATE
        -w
    )
endif()

add_library(tree_sitter::tree_sitter ALIAS tree_sitter_core)

# ── tree_sitter::hlsl ───────────────────────────────────────────────────────

set(_HLSL_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter-hlsl/src")

set(_HLSL_SOURCES
    "${_HLSL_SRC_DIR}/parser.c"
)

# scanner.c exists in this grammar - include it.
if(EXISTS "${_HLSL_SRC_DIR}/scanner.c")
    list(APPEND _HLSL_SOURCES "${_HLSL_SRC_DIR}/scanner.c")
endif()

add_library(tree_sitter_hlsl_lang STATIC ${_HLSL_SOURCES})

set_source_files_properties(${_HLSL_SOURCES}
    PROPERTIES LANGUAGE C
)

target_include_directories(tree_sitter_hlsl_lang
    PUBLIC
        "${_HLSL_SRC_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/include"
)

# Suppress warnings from generated grammar C code.
if(MSVC)
    target_compile_options(tree_sitter_hlsl_lang PRIVATE
        /W0
        /wd4100 /wd4200 /wd4244 /wd4245 /wd4267
    )
else()
    target_compile_options(tree_sitter_hlsl_lang PRIVATE
        -w
    )
endif()

target_link_libraries(tree_sitter_hlsl_lang
    PUBLIC tree_sitter_core
)

add_library(tree_sitter::hlsl ALIAS tree_sitter_hlsl_lang)
