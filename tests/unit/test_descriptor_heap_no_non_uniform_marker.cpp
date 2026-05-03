// End-to-end tests for descriptor-heap-no-non-uniform-marker.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

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

TEST_CASE("descriptor-heap-no-non-uniform-marker attaches a wrap Fix",
          "[rules][descriptor-heap-no-non-uniform-marker][fix]") {
    // The wrap is suggestion-grade because the rule cannot prove the index is
    // divergent at every call site; wrapping a uniform index is harmless but
    // may slow the uniform path slightly.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(uint matId : TEXCOORD0) : SV_Target {
    Texture2D tex = ResourceDescriptorHeap[matId];
    return float4(0, 0, 0, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "descriptor-heap-no-non-uniform-marker") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK_FALSE(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement == "NonUniformResourceIndex(matId)");
    }
    CHECK(saw);
}
