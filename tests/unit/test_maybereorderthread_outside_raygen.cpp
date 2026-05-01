// Tests for the maybereorderthread-outside-raygen SM 6.9 SER rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_maybereorderthread_outside_raygen();
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
    rules.push_back(hlsl_clippy::rules::make_maybereorderthread_outside_raygen());
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

TEST_CASE("maybereorderthread-outside-raygen fires on closesthit shader call",
          "[rules][maybereorderthread-outside-raygen]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void OnHit() {
    MaybeReorderThread(hit);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "maybereorderthread-outside-raygen"));
}

TEST_CASE("maybereorderthread-outside-raygen does not fire from raygen",
          "[rules][maybereorderthread-outside-raygen]") {
    const std::string hlsl = R"hlsl(
[shader("raygeneration")]
void RayGen() {
    MaybeReorderThread(hit);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "maybereorderthread-outside-raygen");
    }
}
