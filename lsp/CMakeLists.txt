cmake_minimum_required(VERSION 3.20)

# hlsl-clippy-lsp — JSON-RPC LSP frontend (Phase 5 sub-phase 5a per
# ADR 0014). Thin wrapper around `core`: translates LSP requests into
# `lint(...)` calls and translates `Diagnostic` / `Fix` back out into LSP
# JSON. No rule logic lives here.

add_executable(hlsl_clippy_lsp
    src/main.cpp
    src/rpc/framing.cpp
    src/rpc/dispatcher.cpp
    src/document/manager.cpp
    src/document/uri.cpp
    src/server/handlers.cpp
    src/server/diagnostic_convert.cpp
    src/server/code_actions.cpp
    src/config_resolver.cpp
)

set_target_properties(hlsl_clippy_lsp PROPERTIES OUTPUT_NAME "hlsl-clippy-lsp")

target_include_directories(hlsl_clippy_lsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(hlsl_clippy_lsp PRIVATE
    hlsl_clippy_core
    hlsl_clippy_warnings
    nlohmann_json::nlohmann_json
)

target_compile_features(hlsl_clippy_lsp PRIVATE cxx_std_23)

# Windows: deploy runtime DLLs (slang.dll etc.) next to the LSP exe so it
# runs out of the build tree without requiring the user to mutate PATH.
# No-op on Linux/macOS (RPATH handles it).
if(WIN32)
    add_custom_command(TARGET hlsl_clippy_lsp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:hlsl_clippy_lsp>
            $<TARGET_FILE_DIR:hlsl_clippy_lsp>
        COMMAND_EXPAND_LISTS
    )
endif()
