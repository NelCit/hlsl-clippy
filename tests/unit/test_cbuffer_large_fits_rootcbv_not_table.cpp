// End-to-end tests for the cbuffer-large-fits-rootcbv-not-table rule
// (Pack C, Phase 3 reflection-aware).
//
// The rule fires on cbuffers whose total size is greater than 32 bytes
// (above the root-constants sweet spot) and at most 65536 bytes (below
// the cbuffer cap), suggesting a root CBV instead of a descriptor table.

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
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_large_fits_rootcbv_not_table();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources,
                                                  const std::string& path = "synthetic.hlsl") {
    const auto src = sources.add_buffer(path, hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_cbuffer_large_fits_rootcbv_not_table());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
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

[[nodiscard]] std::size_t count_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    std::size_t n = 0;
    for (const auto& d : diags) {
        if (d.code == code) {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("cbuffer-large-fits-rootcbv-not-table fires on a 64 byte cbuffer",
          "[rules][cbuffer-large-fits-rootcbv-not-table]") {
    SourceManager sources;
    // float4 (16) + float4 (16) + float4 (16) + float4 (16) = 64 bytes.
    const std::string hlsl = R"hlsl(
cbuffer SceneConstants
{
    float4 view_dir;
    float4 sun_dir;
    float4 ambient;
    float4 fog_params;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return view_dir + sun_dir + ambient + fog_params;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "cbuffer-large-fits-rootcbv-not-table"));
}

TEST_CASE("cbuffer-large-fits-rootcbv-not-table does not fire on a small cbuffer",
          "[rules][cbuffer-large-fits-rootcbv-not-table]") {
    SourceManager sources;
    // float4 (16) only -> 16 bytes -> below the 32-byte threshold.
    const std::string hlsl = R"hlsl(
cbuffer Tiny
{
    float4 only_field;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return only_field;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "cbuffer-large-fits-rootcbv-not-table");
    }
}

TEST_CASE("cbuffer-large-fits-rootcbv-not-table fires on 1 KB matrix-heavy cbuffer",
          "[rules][cbuffer-large-fits-rootcbv-not-table]") {
    SourceManager sources;
    // Sixteen float4x4 (64 bytes each) = 1024 bytes -> well within 64 KB.
    const std::string hlsl = R"hlsl(
cbuffer LightMatrices
{
    float4x4 mats[16];
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return mats[0]._m00_m01_m02_m03;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "cbuffer-large-fits-rootcbv-not-table"));
}

TEST_CASE(
    "cbuffer-large-fits-rootcbv-not-table fires once per qualifying cbuffer in a multi-cbuffer "
    "source",
    "[rules][cbuffer-large-fits-rootcbv-not-table]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Big1
{
    float4 a;
    float4 b;
    float4 c;
};

cbuffer Big2
{
    float4x4 mat;
    float4   extra;
};

cbuffer Tiny
{
    float one_value;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return a + b + c + extra + mat._m00_m01_m02_m03 + one_value;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    // Big1 (48 bytes) and Big2 (80 bytes) both fire; Tiny (4 bytes) does not.
    CHECK(count_rule(diags, "cbuffer-large-fits-rootcbv-not-table") >= 2U);
}

TEST_CASE("cbuffer-large-fits-rootcbv-not-table emits a suggestion-grade diagnostic with no fix",
          "[rules][cbuffer-large-fits-rootcbv-not-table]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Material
{
    float4 base_color;
    float4 emissive;
    float4 specular;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color + emissive + specular;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "cbuffer-large-fits-rootcbv-not-table") {
            hit = &d;
            break;
        }
    }
    REQUIRE(hit != nullptr);
    CHECK(hit->fixes.empty());
}
