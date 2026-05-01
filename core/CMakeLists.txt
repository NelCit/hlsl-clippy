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
    src/rules/manual_refract.cpp
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

    # Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
    src/rules/cross_with_up_vector.cpp
    src/rules/groupshared_volatile.cpp
    src/rules/lerp_on_bool_cond.cpp
    src/rules/select_vs_lerp_of_constant.cpp
    src/rules/redundant_unorm_snorm_conversion.cpp
    src/rules/wavereadlaneat_constant_zero_to_readfirst.cpp
    src/rules/loop_attribute_conflict.cpp

    # Phase 3 — reflection infrastructure (ADR 0012)
    src/reflection/reflection.cpp
    src/reflection/engine.cpp
    src/reflection/slang_bridge.cpp

    # Phase 3 — shared utilities for reflection-aware rules (ADR 0012 sub-phase 3b).
    src/rules/util/reflect_resource.cpp
    src/rules/util/reflect_sampler.cpp
    src/rules/util/reflect_stage.cpp
)

target_include_directories(hlsl_clippy_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(hlsl_clippy_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(hlsl_clippy_core PUBLIC cxx_std_23)

target_link_libraries(hlsl_clippy_core
    PRIVATE
        hlsl_clippy_warnings
        tree_sitter::tree_sitter
        tree_sitter::hlsl
        tomlplusplus::tomlplusplus
        # Phase 3 — reflection infrastructure (ADR 0012). Slang stays PRIVATE
        # so it never appears in the public include path; only
        # core/src/reflection/slang_bridge.cpp pulls in <slang.h>.
        slang::slang
)

# slang_bridge.cpp needs the Slang public headers; those are propagated to
# `core` compiles via the slang::slang imported target's
# INTERFACE_INCLUDE_DIRECTORIES (set by cmake/UseSlang.cmake). The PRIVATE
# link above keeps Slang headers off the public include path of `core` so
# they remain invisible to CLI / LSP / test consumers.
