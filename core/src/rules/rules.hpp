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

}  // namespace hlsl_clippy::rules
