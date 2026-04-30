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
    return rules;
}

}  // namespace hlsl_clippy
