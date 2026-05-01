// End-to-end tests for descriptor-heap-type-confusion.

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
    const auto src = sources.add_buffer("dhc.hlsl", hlsl);
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

TEST_CASE("descriptor-heap-type-confusion fires on sampler heap to texture",
          "[rules][descriptor-heap-type-confusion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(uint i : TEXCOORD0) : SV_Target {
    Texture2D tex = SamplerDescriptorHeap[i];
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "descriptor-heap-type-confusion"));
}

TEST_CASE("descriptor-heap-type-confusion does not fire when types match",
          "[rules][descriptor-heap-type-confusion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(uint i : TEXCOORD0) : SV_Target {
    SamplerState s = SamplerDescriptorHeap[i];
    Texture2D    t = ResourceDescriptorHeap[i];
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "descriptor-heap-type-confusion"));
}
