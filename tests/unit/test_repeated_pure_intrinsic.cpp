// End-to-end tests for the repeated-pure-intrinsic rule.
//
// Function-scope detector: flags two or more syntactically-identical calls
// to an expensive pure intrinsic when no intervening mutation could have
// changed the argument's value. Suggestion-only fix (hoisting).

#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] std::size_t count_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    std::size_t n = 0U;
    for (const auto& d : diags) {
        if (d.code == code) {
            ++n;
        }
    }
    return n;
}

constexpr std::string_view k_rule = "repeated-pure-intrinsic";

}  // namespace

// ---- positive cases ----

TEST_CASE("repeated-pure-intrinsic fires on the IOR snippet (`sqrt(f0)` twice in one expression)",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ior_index(float f0, float maxIor)
{
    float iorIndex = 1.0;
    if (f0 < 1)
    {
        float ior = (sqrt(f0) + 1.0f) / (1.0f - sqrt(f0));
        iorIndex = saturate((ior - 1.0f) / (maxIor - 1.0f));
    }
    return iorIndex;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 1U);
}

TEST_CASE("repeated-pure-intrinsic fires across statements in the same function",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 foo(float3 N, float3 L)
{
    float3 a = normalize(N);
    float3 b = normalize(N) * 0.5;
    return a + b;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 1U);
}

TEST_CASE("repeated-pure-intrinsic fires on triple duplicate (two diagnostics, paired)",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x)
{
    float a = pow(x, 2.5);
    float b = pow(x, 2.5);
    float c = pow(x, 2.5);
    return a + b + c;
}
)hlsl";
    // Three identical calls -> two consecutive-pair reports (B vs A, C vs B).
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 2U);
}

// ---- negative cases (mutation between calls suppresses the report) ----

TEST_CASE("repeated-pure-intrinsic stays silent when the argument is reassigned between calls",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x)
{
    float a = sqrt(x);
    x = x * 2.0;
    float b = sqrt(x);
    return a + b;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent when the argument is incremented between calls",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x)
{
    float a = exp(x);
    x += 1.0;
    float b = exp(x);
    return a + b;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent across a non-pure call that touches the argument",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void mutate_x(inout float x);

float foo(float x)
{
    float a = log(x);
    mutate_x(x);
    float b = log(x);
    return a + b;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent when args differ even slightly",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x, float y)
{
    float a = pow(x, 2.0);
    float b = pow(x, 3.0);
    return a + b;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent across function boundaries",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x) { return sqrt(x); }
float bar(float x) { return sqrt(x); }
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent on cheap intrinsics (min/max/abs/dot/lerp)",
          "[rules][repeated-pure-intrinsic]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x, float y)
{
    float a = min(x, y);
    float b = min(x, y);
    float c = abs(x);
    float d = abs(x);
    return a + b + c + d;
}
)hlsl";
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

// ---- v2.0.4 CFG-aware precision (Stage::ControlFlow + reachability) ----
//
// In v2.0.3 the rule treated any lexical mutation between two duplicate
// calls as a barrier — even if that mutation sat in an early-return
// branch and therefore could not reach the second call along the CFG.
// v2.0.4 routes the mutation filter through the Phase 4 CFG so dead-path
// mutations no longer suppress the report. The cases below would have
// been false negatives in v2.0.3.

TEST_CASE("repeated-pure-intrinsic fires when the mutation sits in an early-return branch",
          "[rules][repeated-pure-intrinsic][cfg]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x, bool early)
{
    float a = sqrt(x);
    if (early)
    {
        x = 0.0;
        return a;
    }
    float b = sqrt(x);
    return a + b;
}
)hlsl";
    // The `x = 0.0` mutation lives in a block that returns and never
    // flows to the second `sqrt(x)`. CFG-aware: NOT a barrier.
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 1U);
}

TEST_CASE("repeated-pure-intrinsic fires when the mutation sits in a `discard` branch",
          "[rules][repeated-pure-intrinsic][cfg]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ps_main(float x : TEXCOORD0) : SV_Target
{
    float a = sqrt(x);
    if (x < 0.0)
    {
        x = 1.0;
        discard;
    }
    float b = sqrt(x);
    return a + b;
}
)hlsl";
    // `discard` blocks don't flow to the join — same logic as early
    // return.
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 1U);
}

TEST_CASE("repeated-pure-intrinsic stays silent when the mutation reaches both calls",
          "[rules][repeated-pure-intrinsic][cfg]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x, bool b)
{
    float a = sqrt(x);
    if (b)
    {
        x = x + 1.0;
        // No early exit — control flow joins back below.
    }
    float c = sqrt(x);
    return a + c;
}
)hlsl";
    // The `if (b)` branch joins back into the linear flow, so `x = x + 1`
    // CAN reach the second `sqrt(x)`. Conservative suppression preserved.
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}

TEST_CASE("repeated-pure-intrinsic stays silent when the two calls live on disjoint branches",
          "[rules][repeated-pure-intrinsic][cfg]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float foo(float x, int mode)
{
    if (mode == 0)
    {
        return sqrt(x);
    }
    else
    {
        return sqrt(x) * 2.0;
    }
}
)hlsl";
    // Neither call dominates the other — they're on disjoint paths from
    // the function entry. CFG-aware dominator check suppresses; the user
    // genuinely needs both call sites because at most one runs.
    CHECK(count_rule(lint_buffer(hlsl, sources), k_rule) == 0U);
}
