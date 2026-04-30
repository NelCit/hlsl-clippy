add_library(hlsl_clippy_core STATIC
    src/version.cpp
)

target_include_directories(hlsl_clippy_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(hlsl_clippy_core PUBLIC cxx_std_20)
