// End-to-end tests for the primcount-overrun-in-conditional-cf rule.
// `SetMeshOutputCounts(...)` must execute exactly once with thread-uniform
// counts before any primitive write — divergent counts or divergent-CF
// reach are UB on RDNA / Ada / Xe-HPG.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<shader_clippy::Rule> make_primcount_overrun_in_conditional_cf();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_primcount_overrun_in_conditional_cf());
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

TEST_CASE("primcount-overrun-in-conditional-cf fires when count is divergent",
          "[rules][primcount-overrun-in-conditional-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID)
{
    SetMeshOutputCounts(64, gtid);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "primcount-overrun-in-conditional-cf"));
}

TEST_CASE("primcount-overrun-in-conditional-cf fires when call sits in divergent if",
          "[rules][primcount-overrun-in-conditional-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID)
{
    if (gtid > 0) {
        SetMeshOutputCounts(64, 64);
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "primcount-overrun-in-conditional-cf"));
}

TEST_CASE("primcount-overrun-in-conditional-cf does not fire on uniform-call shape",
          "[rules][primcount-overrun-in-conditional-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID)
{
    SetMeshOutputCounts(64, 64);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "primcount-overrun-in-conditional-cf"));
}

TEST_CASE("primcount-overrun-in-conditional-cf does not fire when call is absent",
          "[rules][primcount-overrun-in-conditional-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID)
{
    if (gtid > 0) {
        // no SetMeshOutputCounts here
    }
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "primcount-overrun-in-conditional-cf"));
}
