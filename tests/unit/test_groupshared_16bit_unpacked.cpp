// End-to-end tests for the groupshared-16bit-unpacked rule.
// Stage::Ast -- detects `groupshared` 16-bit arrays whose every read site
// widens to 32-bit operands, which collapses the LDS-bandwidth saving
// 16-bit lanes give on RDNA 2/3.

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
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_groupshared_16bit_unpacked();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_groupshared_16bit_unpacked());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rule();
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

TEST_CASE("groupshared-16bit-unpacked fires on min16float widened to 32-bit literal",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared min16float gs_buf[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float widened = gs_buf[tid.x] * 2.0;
    (void)widened;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-16bit-unpacked"));
}

TEST_CASE("groupshared-16bit-unpacked fires on uint16_t widened via cast",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared uint16_t gs_idx[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint widened = (uint)gs_idx[tid.x];
    (void)widened;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-16bit-unpacked"));
}

TEST_CASE("groupshared-16bit-unpacked does not fire on a 32-bit groupshared array",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gs_buf[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = gs_buf[tid.x] * 2.0;
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-16bit-unpacked");
}

TEST_CASE("groupshared-16bit-unpacked does not fire when read kept in min16float",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared min16float gs_buf[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    min16float kept = gs_buf[tid.x];
    (void)kept;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-16bit-unpacked");
}

TEST_CASE("groupshared-16bit-unpacked does not fire on a non-array groupshared scalar",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared min16float gs_scalar;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = gs_scalar * 2.0;
    (void)v;
}
)hlsl";
    // The rule targets array-shaped declarations; scalar groupshared 16-bit
    // is not in scope (the `[` test in collect_decls excludes it).
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-16bit-unpacked");
}

TEST_CASE("groupshared-16bit-unpacked does not fire when there are no observed reads",
          "[rules][groupshared-16bit-unpacked]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared min16float gs_buf[256];

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    gs_buf[tid.x] = (min16float)0.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-16bit-unpacked");
}
