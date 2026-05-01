// End-to-end tests for the groupshared-volatile rule.
// `volatile groupshared float foo;` is meaningless under HLSL's memory model.

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

TEST_CASE("groupshared-volatile fires on volatile groupshared float",
          "[rules][groupshared-volatile]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
volatile groupshared float gShared[64];
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-volatile"));
}

TEST_CASE("groupshared-volatile fires on groupshared volatile float",
          "[rules][groupshared-volatile]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared volatile float gShared[64];
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-volatile"));
}

TEST_CASE("groupshared-volatile does not fire on plain groupshared",
          "[rules][groupshared-volatile]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gShared[64];
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "groupshared-volatile");
}

TEST_CASE("groupshared-volatile does not fire on volatile-only locals",
          "[rules][groupshared-volatile]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() { volatile float x = 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "groupshared-volatile");
}

TEST_CASE("groupshared-volatile fix removes the volatile token",
          "[rules][groupshared-volatile][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared volatile float gShared[64];
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "groupshared-volatile");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement.empty());
}
