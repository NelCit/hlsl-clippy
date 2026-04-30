// Internal-only header listing the factory functions for the rules shipped in
// the default pack. Each rule has its own translation unit; the registry pulls
// them all in.

#pragma once

#include <memory>

#include "hlsl_clippy/rule.hpp"

namespace hlsl_clippy::rules {

[[nodiscard]] std::unique_ptr<Rule> make_pow_const_squared();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_saturate();

}  // namespace hlsl_clippy::rules
