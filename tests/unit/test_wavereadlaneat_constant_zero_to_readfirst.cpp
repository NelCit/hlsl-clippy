// End-to-end tests for the wavereadlaneat-constant-zero-to-readfirst rule.
// `WaveReadLaneAt(x, 0)` -> `WaveReadLaneFirst(x)`, machine-applicable.

#include <string>
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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("wavereadlaneat-constant-zero-to-readfirst fires on WaveReadLaneAt(x, 0)",
          "[rules][wavereadlaneat-constant-zero-to-readfirst]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return WaveReadLaneAt(x, 0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources),
                   "wavereadlaneat-constant-zero-to-readfirst"));
}

TEST_CASE("wavereadlaneat-constant-zero-to-readfirst fires on WaveReadLaneAt(x, 0u)",
          "[rules][wavereadlaneat-constant-zero-to-readfirst]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return WaveReadLaneAt(x, 0u); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources),
                   "wavereadlaneat-constant-zero-to-readfirst"));
}

TEST_CASE("wavereadlaneat-constant-zero-to-readfirst does not fire on WaveReadLaneAt(x, 1)",
          "[rules][wavereadlaneat-constant-zero-to-readfirst]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return WaveReadLaneAt(x, 1); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavereadlaneat-constant-zero-to-readfirst");
    }
}

TEST_CASE("wavereadlaneat-constant-zero-to-readfirst does not fire on dynamic lane",
          "[rules][wavereadlaneat-constant-zero-to-readfirst]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x, uint lane) { return WaveReadLaneAt(x, lane); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavereadlaneat-constant-zero-to-readfirst");
    }
}

TEST_CASE("wavereadlaneat-constant-zero-to-readfirst fix replaces with WaveReadLaneFirst",
          "[rules][wavereadlaneat-constant-zero-to-readfirst][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return WaveReadLaneAt(x, 0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "wavereadlaneat-constant-zero-to-readfirst");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "WaveReadLaneFirst(x)");
}
