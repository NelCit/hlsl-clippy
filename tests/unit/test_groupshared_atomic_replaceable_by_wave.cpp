// Tests for groupshared-atomic-replaceable-by-wave.
//
// Stage::Ast: detects `Interlocked{Add,Or,...}(gs[const], ...)` against a
// single groupshared cell where a wave-reduce + one-lane atomic would
// replace the per-lane LDS-atomic round-trips.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_atomic_replaceable_by_wave();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_groupshared_atomic_replaceable_by_wave());
    return lint(sources, src, rules);
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

TEST_CASE("groupshared-atomic-replaceable-by-wave fires on InterlockedAdd to scalar groupshared",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Counter;
RWStructuredBuffer<uint> Out;
[numthreads(64, 1, 1)]
void cs(uint dtid : SV_DispatchThreadID) {
    InterlockedAdd(g_Counter, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-atomic-replaceable-by-wave"));
}

TEST_CASE("groupshared-atomic-replaceable-by-wave fires on InterlockedOr to gs[0]",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Mask[1];
[numthreads(64, 1, 1)]
void cs(uint dtid : SV_DispatchThreadID) {
    InterlockedOr(g_Mask[0], 1u);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-atomic-replaceable-by-wave"));
}

TEST_CASE("groupshared-atomic-replaceable-by-wave fires on InterlockedMin to gs[0]",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_MinDepth[1];
[numthreads(64, 1, 1)]
void cs(uint dtid : SV_DispatchThreadID) {
    InterlockedMin(g_MinDepth[0], dtid);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-atomic-replaceable-by-wave"));
}

TEST_CASE("groupshared-atomic-replaceable-by-wave silent on per-lane variable index",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Bins[64];
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    InterlockedAdd(g_Bins[gi], 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-atomic-replaceable-by-wave");
    }
}

TEST_CASE("groupshared-atomic-replaceable-by-wave silent on non-groupshared atomic",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> g_Out;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    InterlockedAdd(g_Out[0], 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-atomic-replaceable-by-wave");
    }
}

TEST_CASE("groupshared-atomic-replaceable-by-wave silent on non-Interlocked call",
          "[rules][groupshared-atomic-replaceable-by-wave]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Counter;
[numthreads(64, 1, 1)]
void cs(uint dtid : SV_DispatchThreadID) {
    g_Counter = 0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-atomic-replaceable-by-wave");
    }
}
