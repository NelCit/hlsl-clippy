// Tests for the omm-allocaterayquery2-non-const-flags rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_omm_allocaterayquery2_non_const_flags();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_omm_allocaterayquery2_non_const_flags());
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

TEST_CASE("omm-allocaterayquery2-non-const-flags fires on identifier first arg",
          "[rules][omm-allocaterayquery2-non-const-flags]") {
    const std::string hlsl = R"hlsl(
void f() {
    RayQuery q = AllocateRayQuery2(g_RuntimeFlags, RAY_FLAG_NONE);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "omm-allocaterayquery2-non-const-flags"));
}

TEST_CASE("omm-allocaterayquery2-non-const-flags silent on RAY_FLAG_* first arg",
          "[rules][omm-allocaterayquery2-non-const-flags]") {
    const std::string hlsl = R"hlsl(
void f() {
    RayQuery q = AllocateRayQuery2(
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
        g_RuntimeFlags);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "omm-allocaterayquery2-non-const-flags");
    }
}
