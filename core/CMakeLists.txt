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

    # Phase 2 — saturate-redundancy + bit-manipulation rules.
    src/rules/redundant_normalize.cpp
    src/rules/redundant_transpose.cpp
    src/rules/redundant_abs.cpp
    src/rules/countbits_vs_manual_popcount.cpp
    src/rules/firstbit_vs_log2_trick.cpp
    src/rules/manual_mad_decomposition.cpp

    # Phase 2 — pow + vec/length rules.
    src/rules/pow_to_mul.cpp
    src/rules/pow_base_two_to_exp2.cpp
    src/rules/pow_integer_decomposition.cpp
    src/rules/inv_sqrt_to_rsqrt.cpp
    src/rules/manual_distance.cpp
    src/rules/length_comparison.cpp
    src/rules/length_then_divide.cpp
    src/rules/dot_on_axis_aligned_vector.cpp

    # Phase 2 — misc / numerical safety rules.
    src/rules/compare_equal_float.cpp
    src/rules/comparison_with_nan_literal.cpp
    src/rules/redundant_precision_cast.cpp
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
