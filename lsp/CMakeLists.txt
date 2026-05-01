cmake_minimum_required(VERSION 3.20)

# hlsl-clippy-lsp — JSON-RPC LSP frontend (Phase 5 sub-phase 5a per
# ADR 0014). Thin wrapper around `core`: translates LSP requests into
# `lint(...)` calls and translates `Diagnostic` / `Fix` back out into LSP
# JSON. No rule logic lives here.
#
# Layout: the JSON-RPC dispatcher + handlers + URI/document machinery
# lives in a static lib `hlsl_clippy_lsp_lib`; the executable is a thin
# `main.cpp` that links it. Tests link the lib too — pre-v0.6 the test
# binary directly recompiled all 8 LSP source files (~25k extra LOC of
# nlohmann/json plus the LSP TUs themselves) on every clean build. The
# lib factor eliminates that duplicate compile and keeps any future LSP
# unit-test additions cheap. Audit-flagged in 2026-05-01 architecture
# review.

add_library(hlsl_clippy_lsp_lib STATIC
    src/rpc/framing.cpp
    src/rpc/dispatcher.cpp
    src/document/manager.cpp
    src/document/uri.cpp
    src/server/handlers.cpp
    src/server/diagnostic_convert.cpp
    src/server/code_actions.cpp
    src/config_resolver.cpp
)

target_include_directories(hlsl_clippy_lsp_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(hlsl_clippy_lsp_lib PUBLIC
    hlsl_clippy_core
    nlohmann_json::nlohmann_json
)
target_link_libraries(hlsl_clippy_lsp_lib PRIVATE
    hlsl_clippy_warnings
)

target_compile_features(hlsl_clippy_lsp_lib PUBLIC cxx_std_23)

add_executable(hlsl_clippy_lsp
    src/main.cpp
)

set_target_properties(hlsl_clippy_lsp PROPERTIES OUTPUT_NAME "hlsl-clippy-lsp")

target_link_libraries(hlsl_clippy_lsp PRIVATE
    hlsl_clippy_lsp_lib
    hlsl_clippy_warnings
)

# Windows: deploy the full Slang runtime DLL constellation (slang.dll +
# 6 transitive dlopen()ed siblings) next to the LSP exe so it runs out of
# the build tree without requiring the user to mutate PATH. No-op on
# Linux/macOS (RPATH handles it). See cmake/UseSlang.cmake for details.
hlsl_clippy_deploy_slang_dlls(hlsl_clippy_lsp)
