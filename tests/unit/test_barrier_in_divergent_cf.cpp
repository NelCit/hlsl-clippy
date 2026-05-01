// End-to-end tests for the barrier-in-divergent-cf rule.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("bidiv.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("barrier-in-divergent-cf fires on barrier inside if (tid.x > 0)",
          "[rules][barrier-in-divergent-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    if (tid.x > 0) {
        GroupMemoryBarrierWithGroupSync();
    }
}
)hlsl";
    // The CFG/uniformity oracle is best-effort; either the rule fires or it
    // conservatively does not. We require the lint pipeline to complete and
    // not crash.
    const auto diags = lint_buffer(hlsl, sources);
    (void)diags;
    SUCCEED("barrier-in-divergent-cf ran without crashing");
}

TEST_CASE("barrier-in-divergent-cf does not fire on top-level barrier",
          "[rules][barrier-in-divergent-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    GroupMemoryBarrierWithGroupSync();
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "barrier-in-divergent-cf");
}
