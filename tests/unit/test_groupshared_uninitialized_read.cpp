// End-to-end tests for the groupshared-uninitialized-read rule.

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
    const auto src = sources.add_buffer("gsuir.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("groupshared-uninitialized-read forward-compatible-stub smoke test",
          "[rules][groupshared-uninitialized-read]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gs[64];

[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    float v = gs[tid.x];
}
)hlsl";
    // The underlying engine helper currently returns false unconditionally
    // (sub-phase 4a does not record per-cell first-access). We verify the
    // pipeline does not crash and the rule remains silent until the engine
    // grows the capability. This is the documented stub behaviour.
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-uninitialized-read");
    SUCCEED("groupshared-uninitialized-read ran (stub)");
}
