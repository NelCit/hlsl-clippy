// Unit tests for the Phase 3 shared utilities under
// `core/src/rules/util/reflect_*` (ADR 0012 sub-phase 3b). Coverage:
//   * `is_writable` discriminates writable from read-only resource kinds.
//   * `is_texture` / `is_buffer` / `is_sampler` classify the relevant kinds.
//   * `find_binding_used_by` hits and misses.
//   * `array_size_of` for bounded arrays, unbounded arrays, and missing names.
//   * `sampler_descriptor_for` returns nullopt today (forward-compatible stub).
//   * `find_entry_point` hits and misses.
//   * Stage-classification predicates (pixel / vertex / compute / mesh / RT).
//   * `shader_model_minor` parses `sm_6_*` / `vs_6_*` / `ps_6_*`; rejects bad
//     input.
//   * `target_supports_sm` boundary cases.
//   * `wave_size_for_entry_point` finds `[WaveSize(N)]` and
//     `[WaveSize(min, max)]` in synthetic HLSL.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <tree_sitter/api.h>

#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"
#include "rules/util/reflect_sampler.hpp"
#include "rules/util/reflect_stage.hpp"

#include "parser_internal.hpp"

namespace {

using shader_clippy::AstTree;
using shader_clippy::EntryPointInfo;
using shader_clippy::ReflectionInfo;
using shader_clippy::ResourceBinding;
using shader_clippy::ResourceKind;
using shader_clippy::SourceManager;

namespace util = shader_clippy::rules::util;

[[nodiscard]] ResourceBinding make_binding(std::string name,
                                           ResourceKind kind,
                                           std::optional<std::uint32_t> array_size = std::nullopt) {
    ResourceBinding b;
    b.name = std::move(name);
    b.kind = kind;
    b.array_size = array_size;
    return b;
}

[[nodiscard]] EntryPointInfo make_entry_point(std::string name, std::string stage) {
    EntryPointInfo ep;
    ep.name = std::move(name);
    ep.stage = std::move(stage);
    return ep;
}

[[nodiscard]] ReflectionInfo make_reflection_with_bindings(std::vector<ResourceBinding> bindings) {
    ReflectionInfo r;
    r.bindings = std::move(bindings);
    r.target_profile = "sm_6_6";
    return r;
}

}  // namespace

TEST_CASE("is_writable distinguishes RW resources from read-only ones",
          "[util][reflect][resource]") {
    CHECK(util::is_writable(ResourceKind::RWBuffer));
    CHECK(util::is_writable(ResourceKind::RWTexture2D));
    CHECK(util::is_writable(ResourceKind::RWByteAddressBuffer));
    CHECK(util::is_writable(ResourceKind::RWStructuredBuffer));
    CHECK(util::is_writable(ResourceKind::AppendStructuredBuffer));
    CHECK(util::is_writable(ResourceKind::ConsumeStructuredBuffer));

    CHECK_FALSE(util::is_writable(ResourceKind::Texture2D));
    CHECK_FALSE(util::is_writable(ResourceKind::Buffer));
    CHECK_FALSE(util::is_writable(ResourceKind::ConstantBuffer));
    CHECK_FALSE(util::is_writable(ResourceKind::SamplerState));
    CHECK_FALSE(util::is_writable(ResourceKind::Unknown));
}

TEST_CASE("is_texture / is_buffer / is_sampler classify the relevant kinds",
          "[util][reflect][resource]") {
    CHECK(util::is_texture(ResourceKind::Texture2D));
    CHECK(util::is_texture(ResourceKind::TextureCubeArray));
    CHECK(util::is_texture(ResourceKind::RWTexture3D));
    CHECK(util::is_texture(ResourceKind::FeedbackTexture2D));
    CHECK_FALSE(util::is_texture(ResourceKind::Buffer));
    CHECK_FALSE(util::is_texture(ResourceKind::SamplerState));

    CHECK(util::is_buffer(ResourceKind::Buffer));
    CHECK(util::is_buffer(ResourceKind::RWBuffer));
    CHECK(util::is_buffer(ResourceKind::ByteAddressBuffer));
    CHECK(util::is_buffer(ResourceKind::StructuredBuffer));
    CHECK(util::is_buffer(ResourceKind::ConstantBuffer));
    CHECK_FALSE(util::is_buffer(ResourceKind::Texture2D));
    CHECK_FALSE(util::is_buffer(ResourceKind::SamplerState));

    CHECK(util::is_sampler(ResourceKind::SamplerState));
    CHECK(util::is_sampler(ResourceKind::SamplerComparisonState));
    CHECK_FALSE(util::is_sampler(ResourceKind::Texture2D));
    CHECK_FALSE(util::is_sampler(ResourceKind::Buffer));
}

TEST_CASE("find_binding_used_by hits and misses on shader-side identifier name",
          "[util][reflect][resource]") {
    auto reflection = make_reflection_with_bindings({
        make_binding("base_color", ResourceKind::Texture2D),
        make_binding("scene_data", ResourceKind::ConstantBuffer),
        make_binding("linear_sampler", ResourceKind::SamplerState),
    });

    const auto* hit = util::find_binding_used_by(reflection, "scene_data");
    REQUIRE(hit != nullptr);
    CHECK(hit->name == "scene_data");
    CHECK(hit->kind == ResourceKind::ConstantBuffer);

    CHECK(util::find_binding_used_by(reflection, "missing_resource") == nullptr);
}

TEST_CASE("array_size_of returns the bound for bounded arrays and nullopt otherwise",
          "[util][reflect][resource]") {
    auto reflection = make_reflection_with_bindings({
        make_binding("bindless_textures", ResourceKind::Texture2D, std::nullopt),
        make_binding("material_data", ResourceKind::StructuredBuffer, std::uint32_t{16}),
        make_binding("single_texture", ResourceKind::Texture2D, std::uint32_t{1}),
    });

    // Bounded: returns the bound.
    const auto bounded = util::array_size_of(reflection, "material_data");
    REQUIRE(bounded.has_value());
    CHECK(*bounded == 16U);

    // Single-element bounded.
    const auto single = util::array_size_of(reflection, "single_texture");
    REQUIRE(single.has_value());
    CHECK(*single == 1U);

    // Unbounded: returns nullopt (matches the binding's array_size field).
    CHECK_FALSE(util::array_size_of(reflection, "bindless_textures").has_value());

    // Missing name: returns nullopt.
    CHECK_FALSE(util::array_size_of(reflection, "ghost_resource").has_value());
}

TEST_CASE("sampler_descriptor_for returns nullopt today (forward-compatible stub)",
          "[util][reflect][sampler]") {
    auto reflection = make_reflection_with_bindings({
        make_binding("linear_sampler", ResourceKind::SamplerState),
        make_binding("shadow_sampler", ResourceKind::SamplerComparisonState),
        make_binding("base_color", ResourceKind::Texture2D),
    });

    // Sampler bindings: bridge does not yet surface descriptor state -> nullopt.
    CHECK_FALSE(util::sampler_descriptor_for(reflection, "linear_sampler").has_value());
    CHECK_FALSE(util::sampler_descriptor_for(reflection, "shadow_sampler").has_value());

    // Non-sampler binding: nullopt by definition.
    CHECK_FALSE(util::sampler_descriptor_for(reflection, "base_color").has_value());

    // Missing binding: nullopt.
    CHECK_FALSE(util::sampler_descriptor_for(reflection, "ghost_sampler").has_value());
}

TEST_CASE("find_entry_point hits and misses on entry-point name", "[util][reflect][stage]") {
    ReflectionInfo r;
    r.target_profile = "sm_6_6";
    r.entry_points.push_back(make_entry_point("vs_main", "vertex"));
    r.entry_points.push_back(make_entry_point("ps_main", "pixel"));

    const auto* hit = util::find_entry_point(r, "ps_main");
    REQUIRE(hit != nullptr);
    CHECK(hit->stage == "pixel");

    CHECK(util::find_entry_point(r, "missing_main") == nullptr);
}

TEST_CASE("stage-classification predicates discriminate by stage tag", "[util][reflect][stage]") {
    const auto pixel = make_entry_point("ps_main", "pixel");
    const auto vertex = make_entry_point("vs_main", "vertex");
    const auto compute = make_entry_point("cs_main", "compute");
    const auto mesh = make_entry_point("ms_main", "mesh");
    const auto amplification = make_entry_point("as_main", "amplification");
    const auto raygen = make_entry_point("rg_main", "raygeneration");
    const auto miss = make_entry_point("miss_main", "miss");

    CHECK(util::is_pixel_shader(pixel));
    CHECK_FALSE(util::is_pixel_shader(vertex));

    CHECK(util::is_vertex_shader(vertex));
    CHECK_FALSE(util::is_vertex_shader(pixel));

    CHECK(util::is_compute_shader(compute));
    CHECK_FALSE(util::is_compute_shader(pixel));

    CHECK(util::is_mesh_or_amp_shader(mesh));
    CHECK(util::is_mesh_or_amp_shader(amplification));
    CHECK_FALSE(util::is_mesh_or_amp_shader(compute));

    CHECK(util::is_raytracing_shader(raygen));
    CHECK(util::is_raytracing_shader(miss));
    CHECK_FALSE(util::is_raytracing_shader(pixel));
    CHECK_FALSE(util::is_raytracing_shader(compute));
}

TEST_CASE("shader_model_minor parses sm_6_* / vs_6_* / ps_6_* and rejects bad input",
          "[util][reflect][stage]") {
    REQUIRE(util::shader_model_minor("sm_6_6").has_value());
    CHECK(*util::shader_model_minor("sm_6_6") == 6U);
    REQUIRE(util::shader_model_minor("sm_6_7").has_value());
    CHECK(*util::shader_model_minor("sm_6_7") == 7U);
    REQUIRE(util::shader_model_minor("sm_6_8").has_value());
    CHECK(*util::shader_model_minor("sm_6_8") == 8U);
    REQUIRE(util::shader_model_minor("sm_6_9").has_value());
    CHECK(*util::shader_model_minor("sm_6_9") == 9U);

    REQUIRE(util::shader_model_minor("vs_6_7").has_value());
    CHECK(*util::shader_model_minor("vs_6_7") == 7U);
    REQUIRE(util::shader_model_minor("ps_6_8").has_value());
    CHECK(*util::shader_model_minor("ps_6_8") == 8U);
    REQUIRE(util::shader_model_minor("cs_6_6").has_value());
    CHECK(*util::shader_model_minor("cs_6_6") == 6U);

    // Bad input: empty, garbage, missing minor, wrong major.
    CHECK_FALSE(util::shader_model_minor("").has_value());
    CHECK_FALSE(util::shader_model_minor("sm_6_").has_value());
    CHECK_FALSE(util::shader_model_minor("sm_5_0").has_value());
    CHECK_FALSE(util::shader_model_minor("sm6_6").has_value());
    CHECK_FALSE(util::shader_model_minor("ham_sandwich").has_value());
}

TEST_CASE("target_supports_sm boundary cases", "[util][reflect][stage]") {
    CHECK(util::target_supports_sm("sm_6_8", 7U));
    CHECK(util::target_supports_sm("sm_6_8", 8U));
    CHECK_FALSE(util::target_supports_sm("sm_6_8", 9U));

    CHECK(util::target_supports_sm("vs_6_7", 6U));
    CHECK(util::target_supports_sm("vs_6_7", 7U));
    CHECK_FALSE(util::target_supports_sm("vs_6_7", 8U));

    CHECK_FALSE(util::target_supports_sm("sm_6_5", 7U));

    // Unparseable profile: returns false (conservative).
    CHECK_FALSE(util::target_supports_sm("not_a_profile", 6U));
    CHECK_FALSE(util::target_supports_sm("", 6U));
}

namespace {

/// Helper that parses synthetic HLSL and constructs an `AstTree` value usable
/// by `wave_size_for_entry_point`. The `ParsedSource` is held by the caller
/// so the tree pointer stays alive for the duration of the test.
[[nodiscard]] shader_clippy::parser::ParsedSource parse_source(
    SourceManager& sources,
    const std::string& hlsl,
    const std::string& name = "wave_size_test.hlsl") {
    const auto src = sources.add_buffer(name, hlsl);
    REQUIRE(src.valid());
    auto parsed_opt = shader_clippy::parser::parse(sources, src);
    REQUIRE(parsed_opt.has_value());
    return std::move(parsed_opt.value());
}

}  // namespace

TEST_CASE("wave_size_for_entry_point finds [WaveSize(32)] on a synthetic compute shader",
          "[util][reflect][stage][wave-size]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};

    const auto ep = make_entry_point("cs_main", "compute");
    const auto result = util::wave_size_for_entry_point(tree, ep);
    REQUIRE(result.has_value());
    CHECK(result->first == 32U);
    CHECK(result->second == 32U);
}

TEST_CASE("wave_size_for_entry_point finds [WaveSize(32, 64)] range",
          "[util][reflect][stage][wave-size]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32, 64)]
void cs_range(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};

    const auto ep = make_entry_point("cs_range", "compute");
    const auto result = util::wave_size_for_entry_point(tree, ep);
    REQUIRE(result.has_value());
    CHECK(result->first == 32U);
    CHECK(result->second == 64U);
}

TEST_CASE("wave_size_for_entry_point returns nullopt when no [WaveSize] is present",
          "[util][reflect][stage][wave-size]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
void cs_no_wave(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};

    const auto ep = make_entry_point("cs_no_wave", "compute");
    CHECK_FALSE(util::wave_size_for_entry_point(tree, ep).has_value());
}

TEST_CASE("wave_size_for_entry_point returns nullopt for an unknown entry-point name",
          "[util][reflect][stage][wave-size]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};

    const auto ep = make_entry_point("not_in_source", "compute");
    CHECK_FALSE(util::wave_size_for_entry_point(tree, ep).has_value());
}
