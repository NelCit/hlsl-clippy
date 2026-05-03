// End-to-end tests for rov-without-earlydepthstencil.

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
    const auto src = sources.add_buffer("rov.hlsl", hlsl);
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

TEST_CASE("rov-without-earlydepthstencil fires on bare ROV usage",
          "[rules][rov-without-earlydepthstencil]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RasterizerOrderedTexture2D<float4> Rov : register(u0);

[shader("pixel")]
float4 ps_main(float4 pos : SV_Position) : SV_Target {
    Rov[uint2(pos.xy)] = float4(1, 0, 0, 1);
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "rov-without-earlydepthstencil"));
}

TEST_CASE("rov-without-earlydepthstencil suppressed on earlydepthstencil",
          "[rules][rov-without-earlydepthstencil]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RasterizerOrderedTexture2D<float4> Rov : register(u0);

[earlydepthstencil]
[shader("pixel")]
float4 ps_main(float4 pos : SV_Position) : SV_Target {
    Rov[uint2(pos.xy)] = float4(1, 0, 0, 1);
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "rov-without-earlydepthstencil"));
}
