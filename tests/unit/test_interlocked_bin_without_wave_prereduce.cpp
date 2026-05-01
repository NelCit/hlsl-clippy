// End-to-end tests for the interlocked-bin-without-wave-prereduce rule.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("ibwp.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
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

TEST_CASE("interlocked-bin-without-wave-prereduce fires on raw InterlockedAdd",
          "[rules][interlocked-bin-without-wave-prereduce]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> Counter;

[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    InterlockedAdd(Counter[0], 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "interlocked-bin-without-wave-prereduce"));
}

TEST_CASE("interlocked-bin-without-wave-prereduce does not fire when WaveActiveSum is present",
          "[rules][interlocked-bin-without-wave-prereduce]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> Counter;

[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    uint sum = WaveActiveSum(1u);
    if (WaveIsFirstLane()) {
        InterlockedAdd(Counter[0], sum);
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "interlocked-bin-without-wave-prereduce");
}
