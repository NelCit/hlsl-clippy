// Smoke tests for the Phase 3 reflection infrastructure (ADR 0012).
//
// Coverage:
//   * `ReflectionEngine` returns a populated `ReflectionInfo` for a simple
//     HLSL source with one cbuffer + one resource binding.
//   * Cache hits on a repeated `(source, target_profile)` tuple do not crash
//     and return equivalent data.
//   * Compile error on bad HLSL surfaces as `Diagnostic` with
//     `code == "clippy::reflection"`.
//   * Multi-entry-point file produces multiple `EntryPointInfo` entries.
//   * `LintOptions::enable_reflection = false` causes the engine to NOT be
//     invoked even when a reflection-stage rule is enabled.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "reflection/engine.hpp"

namespace {

using hlsl_clippy::AstTree;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::ReflectionInfo;
using hlsl_clippy::Rule;
using hlsl_clippy::RuleContext;
using hlsl_clippy::SourceManager;
using hlsl_clippy::Stage;

/// Test rule that records every `on_reflection` invocation it sees so the test
/// can assert the orchestrator dispatched (or did not dispatch) reflection.
class ReflectionSpyRule : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "test::reflection-spy";
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return "test";
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& /*tree*/,
                       const ReflectionInfo& reflection,
                       RuleContext& /*ctx*/) override {
        ++calls;
        last_target_profile = reflection.target_profile;
        last_binding_count = reflection.bindings.size();
        last_cbuffer_count = reflection.cbuffers.size();
        last_entry_point_count = reflection.entry_points.size();
    }

    int calls = 0;
    std::string last_target_profile;
    std::size_t last_binding_count = 0U;
    std::size_t last_cbuffer_count = 0U;
    std::size_t last_entry_point_count = 0U;
};

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

constexpr std::string_view k_one_cbuffer_one_binding = R"hlsl(
cbuffer SceneConstants
{
    float4x4 view_proj;
    float3   camera_pos;
    float    time;
};

Texture2D<float4> base_color;
SamplerState linear_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(linear_sampler, uv);
}
)hlsl";

constexpr std::string_view k_multi_entry_point = R"hlsl(
struct VsOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VsOut vs_main(uint vid : SV_VertexID)
{
    VsOut o;
    o.pos = float4(0.0, 0.0, 0.0, 1.0);
    o.uv  = float2(0.0, 0.0);
    return o;
}

[shader("pixel")]
float4 ps_main(VsOut input) : SV_Target
{
    return float4(input.uv, 0.0, 1.0);
}
)hlsl";

constexpr std::string_view k_bad_source = R"hlsl(
[shader("pixel")]
float4 ps_bad(float2 uv : TEXCOORD0) : SV_Target
{
    // ddxx is not a valid HLSL intrinsic.
    return float4(ddxx(uv), 0.0, 1.0);
}
)hlsl";

}  // namespace

TEST_CASE("ReflectionEngine reflects a simple cbuffer plus one binding", "[reflection][engine]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src =
        sources.add_buffer("simple_cbuffer.hlsl", std::string{k_one_cbuffer_one_binding});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());

    const ReflectionInfo& info = result.value();
    CHECK(info.target_profile == "sm_6_6");
    // The source declares: 1 cbuffer + 1 texture + 1 sampler. We require at
    // least the cbuffer + texture + sampler (Slang may surface them in any
    // order; binding order is not asserted here).
    CHECK(info.cbuffers.size() >= 1U);
    CHECK(info.bindings.size() >= 2U);
    CHECK(info.entry_points.size() >= 1U);
}

TEST_CASE("ReflectionEngine cache hit on repeated reflect call", "[reflection][engine]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("cache_smoke.hlsl", std::string{k_one_cbuffer_one_binding});
    REQUIRE(src.valid());

    const auto first = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(first.has_value());
    const auto first_binding_count = first.value().bindings.size();

    const auto second = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(second.has_value());
    CHECK(second.value().bindings.size() == first_binding_count);
    CHECK(second.value().target_profile == first.value().target_profile);
}

TEST_CASE("ReflectionEngine surfaces bad HLSL as a clippy::reflection diagnostic",
          "[reflection][engine][error]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("bad.hlsl", std::string{k_bad_source});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == "clippy::reflection");
    CHECK(result.error().severity == hlsl_clippy::Severity::Error);
}

TEST_CASE("ReflectionEngine produces multiple EntryPointInfo entries for multi-entry sources",
          "[reflection][engine][entry-points]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("multi_entry.hlsl", std::string{k_multi_entry_point});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());

    const ReflectionInfo& info = result.value();
    CHECK(info.entry_points.size() >= 2U);

    // Lookup by name should succeed for both declared entry points.
    CHECK(info.find_entry_point_by_name("vs_main") != nullptr);
    CHECK(info.find_entry_point_by_name("ps_main") != nullptr);
}

TEST_CASE("LintOptions::enable_reflection = false skips on_reflection dispatch",
          "[reflection][orchestrator]") {
    SourceManager sources;
    const auto src = sources.add_buffer("opt_off.hlsl", std::string{k_one_cbuffer_one_binding});
    REQUIRE(src.valid());

    auto spy = std::make_unique<ReflectionSpyRule>();
    ReflectionSpyRule* spy_raw = spy.get();

    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::move(spy));

    LintOptions options;
    options.enable_reflection = false;

    const auto diagnostics = lint(sources, src, rules, options);

    CHECK(spy_raw->calls == 0);
    CHECK_FALSE(has_rule(diagnostics, "clippy::reflection"));
}

TEST_CASE("LintOptions::enable_reflection = true dispatches on_reflection once",
          "[reflection][orchestrator]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("opt_on.hlsl", std::string{k_one_cbuffer_one_binding});
    REQUIRE(src.valid());

    auto spy = std::make_unique<ReflectionSpyRule>();
    ReflectionSpyRule* spy_raw = spy.get();

    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::move(spy));

    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};

    const auto diagnostics = lint(sources, src, rules, options);

    // We expect exactly one dispatch -- one source, one reflection rule.
    CHECK(spy_raw->calls == 1);
    CHECK(spy_raw->last_target_profile == "sm_6_6");
    CHECK(spy_raw->last_entry_point_count >= 1U);
    // No reflection-failure diagnostic should leak through for a valid source.
    CHECK_FALSE(has_rule(diagnostics, "clippy::reflection"));
}
