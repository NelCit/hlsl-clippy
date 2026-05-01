// End-to-end tests for groupshared-too-large.

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
    const auto src = sources.add_buffer("gtl.hlsl", hlsl);
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

TEST_CASE("groupshared-too-large fires on 64 KB allocation", "[rules][groupshared-too-large]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float Huge[16384];

[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) { Huge[i] = 0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-too-large"));
}

TEST_CASE("groupshared-too-large does not fire on a 1 KB allocation",
          "[rules][groupshared-too-large]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float4 Tile[16];

[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) { Tile[i] = 0; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "groupshared-too-large"));
}
