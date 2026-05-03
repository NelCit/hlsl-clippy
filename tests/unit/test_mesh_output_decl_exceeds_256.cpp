// End-to-end tests for mesh-output-decl-exceeds-256.

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
    const auto src = sources.add_buffer("mod256.hlsl", hlsl);
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

TEST_CASE("mesh-output-decl-exceeds-256 fires on 512 vertices",
          "[rules][mesh-output-decl-exceeds-256]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void ms_main(uint tid : SV_GroupThreadID,
             out vertices V verts[512],
             out indices  uint3 tris[124]) { (void)tid; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mesh-output-decl-exceeds-256"));
}

TEST_CASE("mesh-output-decl-exceeds-256 silent on 64 vertices",
          "[rules][mesh-output-decl-exceeds-256]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void ms_main(uint tid : SV_GroupThreadID,
             out vertices V verts[64],
             out indices  uint3 tris[124]) { (void)tid; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "mesh-output-decl-exceeds-256"));
}
