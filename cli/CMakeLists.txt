add_executable(hlsl-clippy
    src/main.cpp
)

target_link_libraries(hlsl-clippy PRIVATE hlsl_clippy_core hlsl_clippy_warnings)

# Windows: deploy the full Slang runtime DLL constellation (slang.dll +
# 6 transitive dlopen()ed siblings) next to the CLI exe so it runs out of
# the build tree without requiring the user to mutate PATH. No-op on
# Linux/macOS (RPATH handles it). See cmake/UseSlang.cmake for details.
hlsl_clippy_deploy_slang_dlls(hlsl-clippy)
