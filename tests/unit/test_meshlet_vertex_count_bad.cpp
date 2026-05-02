// Tests for the meshlet-vertex-count-bad rule (Phase 7 Pack Mesh; ADR 0017).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_meshlet_vertex_count_bad();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("mvc.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_meshlet_vertex_count_bad());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
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

TEST_CASE("meshlet-vertex-count-bad fires on 200 vertices (above RDNA-optimal 128)",
          "[rules][meshlet-vertex-count-bad]") {
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(out vertices V verts[200], out indices uint3 prims[64]) {
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "meshlet-vertex-count-bad"));
}

TEST_CASE("meshlet-vertex-count-bad fires on 33 vertices (not wave-aligned)",
          "[rules][meshlet-vertex-count-bad]") {
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(out vertices V verts[33], out indices uint3 prims[33]) {
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "meshlet-vertex-count-bad"));
}

TEST_CASE("meshlet-vertex-count-bad silent on 64 vertices (wave-aligned, in budget)",
          "[rules][meshlet-vertex-count-bad]") {
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(out vertices V verts[64], out indices uint3 prims[64]) {
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "meshlet-vertex-count-bad"));
}
