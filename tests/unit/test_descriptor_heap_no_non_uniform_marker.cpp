// End-to-end tests for descriptor-heap-no-non-uniform-marker.

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
    const auto src = sources.add_buffer("dh.hlsl", hlsl);
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

TEST_CASE("descriptor-heap-no-non-uniform-marker fires on dynamic index",
          "[rules][descriptor-heap-no-non-uniform-marker]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(uint matId : TEXCOORD0) : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[matId];
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "descriptor-heap-no-non-uniform-marker"));
}

TEST_CASE("descriptor-heap-no-non-uniform-marker does not fire when marker present",
          "[rules][descriptor-heap-no-non-uniform-marker]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(uint matId : TEXCOORD0) : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(matId)];
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "descriptor-heap-no-non-uniform-marker"));
}

TEST_CASE("descriptor-heap-no-non-uniform-marker does not fire on literal index",
          "[rules][descriptor-heap-no-non-uniform-marker]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[3];
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "descriptor-heap-no-non-uniform-marker"));
}
