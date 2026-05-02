# cmake/UseTreeSitterSlang.cmake
#
# Defines an imported static-library target built from the vendored
# tree-sitter-slang submodule:
#   tree_sitter::slang  - the Slang grammar (parser.c + scanner.c)
#
# Mirrors the shape of `cmake/UseTreeSitter.cmake` for tree_sitter::hlsl.
# tree_sitter::tree_sitter (the core runtime) is provided by UseTreeSitter.cmake
# and is reused unchanged — both grammars link against the same runtime, and
# the runtime is included exactly once in the final binary.
#
# ADR 0021 sub-phase B.1 (v1.4.0). Pinned SHA lives in
# cmake/TreeSitterSlangVersion.cmake.

cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/TreeSitterSlangVersion.cmake")

set(_SLANG_GRAMMAR_SRC_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter-slang/src")

# Sanity check — fail early with a clear message if the submodule wasn't
# initialised. This is the most common build-error surface for new
# contributors who clone without `--recurse-submodules`.
if(NOT EXISTS "${_SLANG_GRAMMAR_SRC_DIR}/parser.c")
    message(FATAL_ERROR
        "tree-sitter-slang submodule is missing parser.c at\n"
        "  ${_SLANG_GRAMMAR_SRC_DIR}\n"
        "Run `git submodule update --init --recursive` from the repo root,\n"
        "or check that external/tree-sitter-slang/ is populated.")
endif()

set(_SLANG_GRAMMAR_SOURCES
    "${_SLANG_GRAMMAR_SRC_DIR}/parser.c"
)

# scanner.c handles raw-string delimiters; ship if present (it is, as of the
# pinned SHA, but the conditional matches the HLSL grammar's pattern).
if(EXISTS "${_SLANG_GRAMMAR_SRC_DIR}/scanner.c")
    list(APPEND _SLANG_GRAMMAR_SOURCES "${_SLANG_GRAMMAR_SRC_DIR}/scanner.c")
endif()

add_library(tree_sitter_slang_lang STATIC ${_SLANG_GRAMMAR_SOURCES})

set_source_files_properties(${_SLANG_GRAMMAR_SOURCES}
    PROPERTIES LANGUAGE C
)

# The grammar's parser.c includes <tree_sitter/parser.h>; that header lives in
# tree-sitter's `lib/include`, exposed PUBLICly by tree_sitter_core. The PUBLIC
# include is also needed downstream (core/src/parser.cpp pulls in
# `<tree_sitter/api.h>`).
#
# scanner.c additionally includes <tree_sitter/alloc.h>, which lives alongside
# the grammar itself in `external/tree-sitter-slang/src/tree_sitter/`. Mirror
# the local-include pattern used by upstream — that header is part of the
# generated grammar's expected layout.
target_include_directories(tree_sitter_slang_lang
    PUBLIC
        "${_SLANG_GRAMMAR_SRC_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../external/tree-sitter/lib/include"
)

# Suppress warnings from generated grammar C code (matches HLSL grammar's
# pattern in `cmake/UseTreeSitter.cmake`).
if(MSVC)
    target_compile_options(tree_sitter_slang_lang PRIVATE
        /W0
        /wd4100 /wd4200 /wd4244 /wd4245 /wd4267
    )
else()
    target_compile_options(tree_sitter_slang_lang PRIVATE
        -w
    )
endif()

target_link_libraries(tree_sitter_slang_lang
    PUBLIC tree_sitter_core
)

add_library(tree_sitter::slang ALIAS tree_sitter_slang_lang)
