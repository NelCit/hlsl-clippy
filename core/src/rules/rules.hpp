// Internal-only header listing the factory functions for the rules shipped in
// the default pack. Each rule has its own translation unit; the registry pulls
// them all in.

#pragma once

#include <memory>

#include "hlsl_clippy/rule.hpp"

namespace hlsl_clippy::rules {

[[nodiscard]] std::unique_ptr<Rule> make_pow_const_squared();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_saturate();
[[nodiscard]] std::unique_ptr<Rule> make_clamp01_to_saturate();

// Phase 2 — math simplification rules.
[[nodiscard]] std::unique_ptr<Rule> make_lerp_extremes();
[[nodiscard]] std::unique_ptr<Rule> make_mul_identity();
[[nodiscard]] std::unique_ptr<Rule> make_sin_cos_pair();
[[nodiscard]] std::unique_ptr<Rule> make_manual_reflect();
[[nodiscard]] std::unique_ptr<Rule> make_manual_step();
[[nodiscard]] std::unique_ptr<Rule> make_manual_smoothstep();

// Phase 2 — saturate-redundancy + bit-manipulation rules.
[[nodiscard]] std::unique_ptr<Rule> make_redundant_normalize();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_transpose();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_abs();
[[nodiscard]] std::unique_ptr<Rule> make_countbits_vs_manual_popcount();
[[nodiscard]] std::unique_ptr<Rule> make_firstbit_vs_log2_trick();
[[nodiscard]] std::unique_ptr<Rule> make_manual_mad_decomposition();

// Phase 2 — pow + vec/length rules.
[[nodiscard]] std::unique_ptr<Rule> make_pow_to_mul();
[[nodiscard]] std::unique_ptr<Rule> make_pow_base_two_to_exp2();
[[nodiscard]] std::unique_ptr<Rule> make_pow_integer_decomposition();
[[nodiscard]] std::unique_ptr<Rule> make_inv_sqrt_to_rsqrt();
[[nodiscard]] std::unique_ptr<Rule> make_manual_distance();
[[nodiscard]] std::unique_ptr<Rule> make_length_comparison();
[[nodiscard]] std::unique_ptr<Rule> make_length_then_divide();
[[nodiscard]] std::unique_ptr<Rule> make_dot_on_axis_aligned_vector();

// Phase 2 — misc / numerical safety rules.
[[nodiscard]] std::unique_ptr<Rule> make_compare_equal_float();
[[nodiscard]] std::unique_ptr<Rule> make_comparison_with_nan_literal();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_precision_cast();

}  // namespace hlsl_clippy::rules
