#include <memory>
#include <vector>

#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"

#include "rules.hpp"

namespace hlsl_clippy {

std::vector<std::unique_ptr<Rule>> make_default_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(rules::make_pow_const_squared());
    rules.push_back(rules::make_redundant_saturate());
    rules.push_back(rules::make_clamp01_to_saturate());
    // Phase 2 — math simplification rules.
    rules.push_back(rules::make_lerp_extremes());
    rules.push_back(rules::make_mul_identity());
    rules.push_back(rules::make_sin_cos_pair());
    rules.push_back(rules::make_manual_reflect());
    rules.push_back(rules::make_manual_step());
    rules.push_back(rules::make_manual_smoothstep());
    // Phase 2 — saturate-redundancy + bit-manipulation rules.
    rules.push_back(rules::make_redundant_normalize());
    rules.push_back(rules::make_redundant_transpose());
    rules.push_back(rules::make_redundant_abs());
    rules.push_back(rules::make_countbits_vs_manual_popcount());
    rules.push_back(rules::make_firstbit_vs_log2_trick());
    rules.push_back(rules::make_manual_mad_decomposition());
    // Phase 2 — pow + vec/length rules.
    rules.push_back(rules::make_pow_to_mul());
    rules.push_back(rules::make_pow_base_two_to_exp2());
    rules.push_back(rules::make_pow_integer_decomposition());
    rules.push_back(rules::make_inv_sqrt_to_rsqrt());
    rules.push_back(rules::make_manual_distance());
    rules.push_back(rules::make_length_comparison());
    rules.push_back(rules::make_length_then_divide());
    rules.push_back(rules::make_dot_on_axis_aligned_vector());
    // Phase 2 — misc / numerical safety rules.
    rules.push_back(rules::make_compare_equal_float());
    rules.push_back(rules::make_comparison_with_nan_literal());
    rules.push_back(rules::make_redundant_precision_cast());
    // Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
    rules.push_back(rules::make_cross_with_up_vector());
    rules.push_back(rules::make_groupshared_volatile());
    rules.push_back(rules::make_lerp_on_bool_cond());
    rules.push_back(rules::make_select_vs_lerp_of_constant());
    rules.push_back(rules::make_redundant_unorm_snorm_conversion());
    rules.push_back(rules::make_wavereadlaneat_constant_zero_to_readfirst());
    rules.push_back(rules::make_loop_attribute_conflict());
    return rules;
}

}  // namespace hlsl_clippy
