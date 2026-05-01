// End-to-end tests for the divergent-buffer-index-on-uniform-resource rule.
// Uniformly-bound resources accessed with a wave-divergent index lose the
// scalar/K$ fast path on RDNA / Ada / Xe-HPG.

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
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_divergent_buffer_index_on_uniform_resource();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_divergent_buffer_index_on_uniform_resource());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rules();
    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = true;
    return lint(sources, src, rules, options);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("divergent-buffer-index-on-uniform-resource fires on SV_DispatchThreadID index",
          "[rules][divergent-buffer-index-on-uniform-resource]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
StructuredBuffer<float> g_Table : register(t0);

[numthreads(64, 1, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID)
{
    float v = g_Table[dtid.x];
    (void)v;
}
)hlsl";
    // Rule depends on the uniformity oracle classifying `dtid.x` as
    // Divergent. The test_cfg.cpp smoke tests assert that the oracle
    // recognises SV_DispatchThreadID-bound identifiers as Divergent;
    // this rule chains on top.
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "divergent-buffer-index-on-uniform-resource"));
}

TEST_CASE("divergent-buffer-index-on-uniform-resource does not fire on uniform literal index",
          "[rules][divergent-buffer-index-on-uniform-resource]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
StructuredBuffer<float> g_Table : register(t0);

[numthreads(64, 1, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID)
{
    float v = g_Table[3];
    (void)v;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "divergent-buffer-index-on-uniform-resource"));
}

TEST_CASE(
    "divergent-buffer-index-on-uniform-resource does not fire on NonUniformResourceIndex-marked",
    "[rules][divergent-buffer-index-on-uniform-resource]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
StructuredBuffer<float> g_Table[64] : register(t0);

float pick(uint matId)
{
    return g_Table[NonUniformResourceIndex(matId)][0];
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "divergent-buffer-index-on-uniform-resource"));
}
