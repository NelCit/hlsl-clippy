add_library(hlsl_clippy_core STATIC
    src/version.cpp
    src/source.cpp
    src/parser.cpp
    src/lint.cpp
    src/rule_context.cpp
    src/rewriter.cpp
    src/suppress.cpp
    src/config.cpp
    src/query/query.cpp
    src/rules/registry.cpp
    src/rules/pow_const_squared.cpp
    src/rules/redundant_saturate.cpp
    src/rules/clamp01_to_saturate.cpp
    src/rules/lerp_extremes.cpp
    src/rules/mul_identity.cpp
    src/rules/sin_cos_pair.cpp
    src/rules/manual_reflect.cpp
    src/rules/manual_step.cpp
    src/rules/manual_smoothstep.cpp
)

target_include_directories(hlsl_clippy_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(hlsl_clippy_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(hlsl_clippy_core PUBLIC cxx_std_20)

target_link_libraries(hlsl_clippy_core
    PRIVATE
        hlsl_clippy_warnings
        tree_sitter::tree_sitter
        tree_sitter::hlsl
        tomlplusplus::tomlplusplus
)
