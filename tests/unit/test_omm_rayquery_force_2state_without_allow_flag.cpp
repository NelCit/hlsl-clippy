// Tests for the omm-rayquery-force-2state-without-allow-flag rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_omm_rayquery_force_2state_without_allow_flag();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_omm_rayquery_force_2state_without_allow_flag());
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("omm-rayquery-force-2state-without-allow-flag fires when allow missing",
          "[rules][omm-rayquery-force-2state-without-allow-flag]") {
    const std::string hlsl = R"hlsl(
void f() {
    RayQuery<RAY_FLAG_FORCE_OMM_2_STATE> q;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "omm-rayquery-force-2state-without-allow-flag"));
}

TEST_CASE("omm-rayquery-force-2state-without-allow-flag silent when both flags coexist",
          "[rules][omm-rayquery-force-2state-without-allow-flag]") {
    const std::string hlsl = R"hlsl(
void f() {
    RayQuery<RAY_FLAG_FORCE_OMM_2_STATE | RAY_FLAG_ALLOW_OPACITY_MICROMAPS> q;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "omm-rayquery-force-2state-without-allow-flag");
    }
}
