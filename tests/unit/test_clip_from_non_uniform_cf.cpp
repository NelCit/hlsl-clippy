// End-to-end tests for the clip-from-non-uniform-cf rule.
// `clip(...)` from non-uniform CF without `[earlydepthstencil]` disables
// early-Z for the whole pipeline state.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_clip_from_non_uniform_cf();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_clip_from_non_uniform_cf());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rules();
    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = true;
    return lint(sources, src, rules, options);
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

TEST_CASE("clip-from-non-uniform-cf fires on clip inside divergent if",
          "[rules][clip-from-non-uniform-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4 ps_main(float4 pos : SV_Position) : SV_Target
{
    if (pos.x > 0.0) {
        clip(pos.y - 0.5);
    }
    return float4(1.0, 1.0, 1.0, 1.0);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "clip-from-non-uniform-cf"));
}

TEST_CASE("clip-from-non-uniform-cf does not fire when [earlydepthstencil] is present",
          "[rules][clip-from-non-uniform-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[earlydepthstencil]
float4 ps_main(float4 pos : SV_Position) : SV_Target
{
    if (pos.x > 0.0) {
        clip(pos.y - 0.5);
    }
    return float4(1.0, 1.0, 1.0, 1.0);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "clip-from-non-uniform-cf"));
}

TEST_CASE("clip-from-non-uniform-cf does not fire when no clip is present",
          "[rules][clip-from-non-uniform-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4 ps_main(float4 pos : SV_Position) : SV_Target
{
    if (pos.x > 0.0) {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    return float4(0.0, 1.0, 0.0, 1.0);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "clip-from-non-uniform-cf"));
}
