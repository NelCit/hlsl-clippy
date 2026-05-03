// End-to-end tests for the rwbuffer-store-without-globallycoherent rule.
// Cross-wave producer/consumer UAV usage must mark the resource
// `globallycoherent` (or split into two dispatches with a UAV barrier).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<shader_clippy::Rule> make_rwbuffer_store_without_globallycoherent();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_rwbuffer_store_without_globallycoherent());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rules();
    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = false;
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

TEST_CASE("rwbuffer-store-without-globallycoherent fires on RWStructuredBuffer producer/consumer",
          "[rules][rwbuffer-store-without-globallycoherent]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> g_Scratch : register(u0);

[numthreads(64, 1, 1)]
void cs_pipeline(uint3 dtid : SV_DispatchThreadID)
{
    g_Scratch[dtid.x] = 1.0;
    float v = g_Scratch[(dtid.x + 1) % 64];
    g_Scratch[dtid.x] = v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "rwbuffer-store-without-globallycoherent"));
}

TEST_CASE("rwbuffer-store-without-globallycoherent does not fire on globallycoherent decl",
          "[rules][rwbuffer-store-without-globallycoherent]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
globallycoherent RWStructuredBuffer<float> g_Scratch : register(u0);

[numthreads(64, 1, 1)]
void cs_pipeline(uint3 dtid : SV_DispatchThreadID)
{
    g_Scratch[dtid.x] = 1.0;
    float v = g_Scratch[(dtid.x + 1) % 64];
    g_Scratch[dtid.x] = v;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "rwbuffer-store-without-globallycoherent"));
}

TEST_CASE("rwbuffer-store-without-globallycoherent does not fire on write-only access",
          "[rules][rwbuffer-store-without-globallycoherent]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> g_Out : register(u0);

[numthreads(64, 1, 1)]
void cs_writeonly(uint3 dtid : SV_DispatchThreadID)
{
    g_Out[dtid.x] = 1.0;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "rwbuffer-store-without-globallycoherent"));
}

TEST_CASE("rwbuffer-store-without-globallycoherent fires on RWByteAddressBuffer Store/Load",
          "[rules][rwbuffer-store-without-globallycoherent]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWByteAddressBuffer g_Bytes : register(u0);

[numthreads(64, 1, 1)]
void cs_bytes(uint3 dtid : SV_DispatchThreadID)
{
    g_Bytes.Store(dtid.x * 4, 1u);
    uint v = g_Bytes.Load((dtid.x + 1) * 4);
    g_Bytes.Store(dtid.x * 4, v);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "rwbuffer-store-without-globallycoherent"));
}
