add_executable(hlsl-clippy
    src/main.cpp
)

target_link_libraries(hlsl-clippy PRIVATE hlsl_clippy_core hlsl_clippy_warnings)

# Windows: deploy runtime DLLs (slang.dll etc.) next to the CLI exe so it
# runs out of the build tree without requiring the user to mutate PATH.
# No-op on Linux/macOS (RPATH handles it).
if(WIN32)
    add_custom_command(TARGET hlsl-clippy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:hlsl-clippy>
            $<TARGET_FILE_DIR:hlsl-clippy>
        COMMAND_EXPAND_LISTS
    )
endif()
