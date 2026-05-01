// End-to-end tests for mesh-numthreads-over-128.

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
    const auto src = sources.add_buffer("mn128.hlsl", hlsl);
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

TEST_CASE("mesh-numthreads-over-128 fires on 16x16x1 mesh", "[rules][mesh-numthreads-over-128]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };

[shader("mesh")]
[numthreads(16, 16, 1)]
[outputtopology("triangle")]
void ms_main(uint tid : SV_GroupThreadID,
             out vertices V verts[64],
             out indices  uint3 tris[124]) { (void)tid; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mesh-numthreads-over-128"));
}

TEST_CASE("mesh-numthreads-over-128 does not fire on 64x1x1 mesh",
          "[rules][mesh-numthreads-over-128]") {
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
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "mesh-numthreads-over-128"));
}
