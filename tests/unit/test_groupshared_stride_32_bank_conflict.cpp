// End-to-end tests for the groupshared-stride-32-bank-conflict rule.

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
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("gs32.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("groupshared-stride-32-bank-conflict fires on gs[tid * 32 + k]",
          "[rules][groupshared-stride-32-bank-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gs[1024];

[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    gs[tid.x * 32 + 0] = 1.0;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-stride-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-32-bank-conflict does not fire on stride 1",
          "[rules][groupshared-stride-32-bank-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gs[64];

[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    gs[tid.x] = 1.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-stride-32-bank-conflict");
}
