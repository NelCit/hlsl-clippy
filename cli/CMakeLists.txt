add_executable(hlsl-clippy
    src/main.cpp
)

target_link_libraries(hlsl-clippy PRIVATE hlsl_clippy_core hlsl_clippy_warnings)
