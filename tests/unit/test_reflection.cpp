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

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reflection/engine.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::AstTree;
using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::ReflectionInfo;
using shader_clippy::Rule;
using shader_clippy::RuleContext;
using shader_clippy::SourceManager;
using shader_clippy::Stage;

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

TEST_CASE("ReflectionEngine resolves configured angle-bracket include roots",
          "[reflection][engine][includes]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      (std::string{"shader-clippy-reflection-include-"} + std::to_string(stamp));
    const auto include_dir = root / "donut" / "include";
    std::filesystem::create_directories(include_dir);
    {
        std::ofstream out(include_dir / "utils.hlsli", std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << "float4 utility_color() { return float4(1.0, 0.0, 0.0, 1.0); }\n";
    }

    SourceManager sources;
    const auto source_path = root / "shaders" / "main.hlsl";
    const auto src = sources.add_buffer(source_path.string(), R"hlsl(
#include <utils.hlsli>

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return utility_color();
}
)hlsl");
    REQUIRE(src.valid());

    const std::vector<std::filesystem::path> include_dirs{include_dir};
    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"}, include_dirs);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    REQUIRE(result.has_value());
    CHECK(result.value().entry_points.size() >= 1U);
}

TEST_CASE("ReflectionEngine reflects a simple cbuffer plus one binding", "[reflection][engine]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
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
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
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

// Regression test for commit 36e7cd4 — Slang's `ISession::loadModuleFromSource\
// String` keys its module dictionary on (name, virtual_path). The bridge
// formerly passed `synthetic` / `synthetic.hlsl` for every call; second and
// later calls in the same process either silently returned the first call's
// reflection (cache hit on Slang's name dictionary) or hit Slang's internal
// `assert: The key already exists in Dictionary`.
//
// Fix: process-lifetime atomic counter appended to module name + virtual
// path on every reflect() call. This test exercises the multi-call path
// with DISTINCT sources to lock the fix in.
TEST_CASE("ReflectionEngine handles multiple successive reflect calls without collision",
          "[reflection][engine][regression]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src_a =
        sources.add_buffer("multi_call_a.hlsl", std::string{k_one_cbuffer_one_binding});
    const auto src_b = sources.add_buffer("multi_call_b.hlsl", std::string{k_multi_entry_point});
    const auto src_c =
        sources.add_buffer("multi_call_c.hlsl", std::string{k_one_cbuffer_one_binding});
    REQUIRE(src_a.valid());
    REQUIRE(src_b.valid());
    REQUIRE(src_c.valid());

    // Three reflect() calls on distinct sources in one process. Pre-fix this
    // would either crash inside Slang or return src_a's reflection for src_b.
    const auto ra = engine.reflect(sources, src_a, std::string_view{"sm_6_6"});
    const auto rb = engine.reflect(sources, src_b, std::string_view{"sm_6_6"});
    const auto rc = engine.reflect(sources, src_c, std::string_view{"sm_6_6"});

    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());
    REQUIRE(rc.has_value());

    // src_a + src_c are textually identical but have different SourceIds; the
    // engine cache keys on (SourceId, target_profile, content fingerprint),
    // so both reflections succeed and have matching shape.
    CHECK(ra.value().bindings.size() == rc.value().bindings.size());
    CHECK(ra.value().cbuffers.size() == rc.value().cbuffers.size());

    // src_b is a multi-entry-point shader; it has no cbuffers and at least
    // two entry points. The pre-fix bug would have returned src_a's data.
    CHECK(rb.value().cbuffers.empty());
    CHECK(rb.value().entry_points.size() >= 2U);
}

TEST_CASE("ReflectionEngine surfaces bad HLSL as a clippy::reflection diagnostic",
          "[reflection][engine][error]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("bad.hlsl", std::string{k_bad_source});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == "clippy::reflection");
    CHECK(result.error().severity == shader_clippy::Severity::Error);
}

TEST_CASE("ReflectionEngine demotes min-precision-only Slang failures to Note severity",
          "[reflection][engine][min-precision]") {
    // Slang's HLSL frontend doesn't recognise DXC's `min16uint` /
    // `min16float` / `min16int` family. Files that legitimately use those
    // (e.g. fixtures exercising the `min16float-in-cbuffer-roundtrip` rule,
    // or shaders ported from DXC where the developer hasn't yet migrated to
    // `float16_t`) used to surface as red Errors in the IDE because the
    // engine emitted Severity::Error unconditionally. Demote to
    // Severity::Note (LSP Information) when the only thing Slang complains
    // about is those types — AST rules still ran, the file is otherwise
    // valid HLSL on a DXC target.
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const std::string buf = R"hlsl(
bool min16uint_equal(min16uint a, min16uint b) {
    return a == b;
}
)hlsl";
    const auto src = sources.add_buffer("min_precision.hlsl", buf);
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == "clippy::reflection");
    CHECK(result.error().severity == shader_clippy::Severity::Note);
    // Message must call out the specific limitation so users can act on it.
    CHECK(result.error().message.find("min16") != std::string::npos);
    CHECK(result.error().message.find("AST-only rules still ran") != std::string::npos);
}

TEST_CASE("ReflectionEngine keeps Severity::Error for non-min-precision Slang failures",
          "[reflection][engine][min-precision]") {
    // Negative test for the demotion above: a file with a *real* user error
    // (undefined identifier that's not a min-precision type) must still
    // surface as Severity::Error so users see it in the Problems panel.
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const std::string buf = R"hlsl(
float ps_main(float2 uv : TEXCOORD) : SV_Target {
    return undefined_function(uv.x);
}
)hlsl";
    const auto src = sources.add_buffer("user_error.hlsl", buf);
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == "clippy::reflection");
    CHECK(result.error().severity == shader_clippy::Severity::Error);
}

TEST_CASE("ReflectionEngine produces multiple EntryPointInfo entries for multi-entry sources",
          "[reflection][engine][entry-points]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
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

// v1.2 (ADR 0019) — DXGI format reflection. The bridge extracts the DXGI
// format from typed resources so rules don't have to AST-parse the template
// arg themselves. Untyped resources surface as the empty string.

constexpr std::string_view k_typed_texture_float4 = R"hlsl(
Texture2D<float4> tex_color;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return tex_color.Load(int3(0, 0, 0));
}
)hlsl";

constexpr std::string_view k_typed_rwtexture_unorm = R"hlsl(
RWTexture2D<unorm float4> tex_unorm;

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    tex_unorm[tid.xy] = float4(0.0, 0.0, 0.0, 0.0);
}
)hlsl";

constexpr std::string_view k_byteaddress_buffer = R"hlsl(
ByteAddressBuffer raw_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint v = raw_buffer.Load(tid.x * 4);
    (void)v;
}
)hlsl";

TEST_CASE("ResourceBinding surfaces DXGI format for `Texture2D<float4>`",
          "[reflection][engine][dxgi-format]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("dxgi_typed.hlsl", std::string{k_typed_texture_float4});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());

    // Find the texture binding and assert its DXGI format. Slang surfaces
    // `float4` as 32-bit-per-channel float -> R32G32B32A32_FLOAT.
    const auto* tex = result.value().find_binding_by_name("tex_color");
    REQUIRE(tex != nullptr);
    CHECK(tex->dxgi_format == "DXGI_FORMAT_R32G32B32A32_FLOAT");
}

TEST_CASE("ResourceBinding either surfaces UNORM or empty for `RWTexture2D<unorm float4>`",
          "[reflection][engine][dxgi-format]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("dxgi_unorm.hlsl", std::string{k_typed_rwtexture_unorm});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());

    const auto* tex = result.value().find_binding_by_name("tex_unorm");
    REQUIRE(tex != nullptr);
    // Known Slang 2026.7.1 ABI limitation: Slang's TypeReflection collapses
    // the `unorm` modifier on a typed-resource template arg (it surfaces
    // `unorm float4` via a wrapper kind that is neither Vector nor Scalar
    // in our enumeration). The bridge therefore returns an empty
    // dxgi_format for `RWTexture2D<unorm float4>` rather than guess at a
    // format. The public-header contract documents "" as a valid result for
    // resources whose format the bridge cannot classify.
    //
    // We assert one of two outcomes:
    //   * A future Slang surfaces the qualifier and the format contains
    //     "UNORM" — preferred, the upgrade is transparent to the rule pack.
    //   * Today's Slang collapses the qualifier and the format is empty —
    //     consumers fall back to AST-side heuristics for the unorm/snorm
    //     determination.
    //
    // Either way, the format MUST NOT be a non-UNORM concrete value (i.e.
    // we must not silently surface `R32G32B32A32_FLOAT` for an
    // explicitly-unorm texture).
    const bool has_unorm = tex->dxgi_format.find("UNORM") != std::string::npos;
    const bool is_empty = tex->dxgi_format.empty();
    INFO("tex_unorm.dxgi_format = '" << tex->dxgi_format << "'");
    CHECK((has_unorm || is_empty));
}

TEST_CASE("ResourceBinding leaves DXGI format empty for `ByteAddressBuffer`",
          "[reflection][engine][dxgi-format]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("dxgi_untyped.hlsl", std::string{k_byteaddress_buffer});
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());

    const auto* buf = result.value().find_binding_by_name("raw_buffer");
    REQUIRE(buf != nullptr);
    CHECK(buf->dxgi_format.empty());
}

TEST_CASE("LintOptions::enable_reflection = true dispatches on_reflection once",
          "[reflection][orchestrator]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
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

// --- v2.0.2 compatibility patchset regression tests -----------------------
//
// Each of the patterns below reflected as a fatal `clippy::reflection`
// Severity::Error before v2.0.2. The compatibility patchset (sampler shim
// prelude + warnings-only severity downgrade + include-roots wiring) keeps
// reflection alive on all three: legacy `sampler` parameter, unknown
// `#pragma pack_matrix`, and angle-bracket includes resolved via
// `LintOptions::include_directories`.

TEST_CASE("Reflection: legacy `sampler` parameter type compiles via shim prelude",
          "[reflection][engine][v2.0.2][regression]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("legacy_sampler.hlsl", R"hlsl(
Texture2D<float4> tex : register(t0);
SamplerState g_sampler : register(s0);

float4 sample_with_legacy_sampler(sampler s, float2 uv)
{
    return tex.Sample(s, uv);
}

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return sample_with_legacy_sampler(g_sampler, uv);
}
)hlsl");
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());
    CHECK(result.value().entry_points.size() >= 1U);
}

TEST_CASE("Reflection: unknown `#pragma pack_matrix` does not produce a fatal Error",
          "[reflection][engine][v2.0.2][regression]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const auto src = sources.add_buffer("pack_matrix.hlsl", R"hlsl(
#pragma pack_matrix(row_major)

cbuffer Globals : register(b0)
{
    float4x4 view_proj;
};

[shader("vertex")]
float4 vs_main(float3 pos : POSITION) : SV_Position
{
    return mul(view_proj, float4(pos, 1.0));
}
)hlsl");
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    if (!result.has_value()) {
        // If Slang surfaces a warnings-only failure here, the bridge must
        // downgrade severity to Note so the IDE doesn't red-screen.
        const auto& diag = result.error();
        CHECK(diag.severity != shader_clippy::Severity::Error);
    } else {
        SUCCEED("reflection succeeded — pack_matrix is accepted by Slang");
    }
}

TEST_CASE("Reflection: angle-bracket include resolves through LintOptions::include_directories",
          "[reflection][engine][v2.0.2][regression]") {
    auto& engine = shader_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() /
                      (std::string{"shader-clippy-v202-angle-include-"} + std::to_string(stamp));
    const auto include_dir = root / "donut" / "shaders";
    std::filesystem::create_directories(include_dir);
    {
        std::ofstream out(include_dir / "helper_functions.hlsli", std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << "float4 helper_color() { return float4(0.5, 0.5, 0.5, 1.0); }\n";
    }

    SourceManager sources;
    const auto source_path = root / "translucent_depth.hlsl";
    const auto src = sources.add_buffer(source_path.string(), R"hlsl(
#include <donut/shaders/helper_functions.hlsli>

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return helper_color();
}
)hlsl");
    REQUIRE(src.valid());

    const std::vector<std::filesystem::path> include_dirs{root};
    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"}, include_dirs);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    REQUIRE(result.has_value());
    CHECK(result.value().entry_points.size() >= 1U);
}
