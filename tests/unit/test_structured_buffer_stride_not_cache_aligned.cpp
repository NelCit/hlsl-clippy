// End-to-end tests for the structured-buffer-stride-not-cache-aligned rule.
// Stage::Reflection -- needs Slang reflection to identify which bindings are
// StructuredBuffer / RWStructuredBuffer; the AST then supplies the per-field
// byte sizes for stride computation.

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
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_structured_buffer_stride_not_cache_aligned();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_structured_buffer_stride_not_cache_aligned());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rule();
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("structured-buffer-stride-not-cache-aligned fires on a 20-byte struct",
          "[rules][structured-buffer-stride-not-cache-aligned]") {
    // Stride = float4 (16) + float (4) = 20 bytes -- multiple of 4, not 64.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct ParticleSmall {
    float4 position;
    float  age;
};

StructuredBuffer<ParticleSmall> particles;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    ParticleSmall p = particles[tid.x];
    (void)p;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "structured-buffer-stride-not-cache-aligned"));
}

TEST_CASE("structured-buffer-stride-not-cache-aligned does not fire on a 64-byte struct",
          "[rules][structured-buffer-stride-not-cache-aligned]") {
    // Stride = 4*float4 = 64 bytes, exactly one cache line.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct ParticleAligned {
    float4 a;
    float4 b;
    float4 c;
    float4 d;
};

StructuredBuffer<ParticleAligned> particles;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    ParticleAligned p = particles[tid.x];
    (void)p;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "structured-buffer-stride-not-cache-aligned");
}

TEST_CASE("structured-buffer-stride-not-cache-aligned does not fire on a non-struct binding",
          "[rules][structured-buffer-stride-not-cache-aligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;
Texture2D<float4> tex;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint4 v = raw_buf.Load4(0);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "structured-buffer-stride-not-cache-aligned");
}

TEST_CASE("structured-buffer-stride-not-cache-aligned fires on RWStructuredBuffer too",
          "[rules][structured-buffer-stride-not-cache-aligned]") {
    // Stride = float3 (12) + float (4) = 16 bytes -- multiple of 4, not 64.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct EntryRecord {
    float3 normal;
    float  weight;
};

RWStructuredBuffer<EntryRecord> records;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    EntryRecord e = records[tid.x];
    (void)e;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "structured-buffer-stride-not-cache-aligned"));
}

TEST_CASE("structured-buffer-stride-not-cache-aligned does not fire on a 128-byte struct",
          "[rules][structured-buffer-stride-not-cache-aligned]") {
    // Stride = 8*float4 = 128 bytes, multiple of 64.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct ParticleBig {
    float4 a; float4 b; float4 c; float4 d;
    float4 e; float4 f; float4 g; float4 h;
};

StructuredBuffer<ParticleBig> particles;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    ParticleBig p = particles[tid.x];
    (void)p;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "structured-buffer-stride-not-cache-aligned");
}
