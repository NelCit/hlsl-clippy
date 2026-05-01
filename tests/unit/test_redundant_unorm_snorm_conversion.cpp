// End-to-end tests for the redundant-unorm-snorm-conversion rule.
// `<expr> / 255.0` and `<expr> * (1.0/255.0)` after a UNORM sample is dead arithmetic.

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

TEST_CASE("redundant-unorm-snorm-conversion fires on / 255.0",
          "[rules][redundant-unorm-snorm-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x / 255.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-unorm-snorm-conversion"));
}

TEST_CASE("redundant-unorm-snorm-conversion fires on / 255.0f",
          "[rules][redundant-unorm-snorm-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x / 255.0f; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-unorm-snorm-conversion"));
}

TEST_CASE("redundant-unorm-snorm-conversion fires on * (1.0 / 255.0)",
          "[rules][redundant-unorm-snorm-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * (1.0 / 255.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-unorm-snorm-conversion"));
}

TEST_CASE("redundant-unorm-snorm-conversion does not fire on / 256.0",
          "[rules][redundant-unorm-snorm-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x / 256.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-unorm-snorm-conversion");
}

TEST_CASE("redundant-unorm-snorm-conversion does not fire on x * 0.5",
          "[rules][redundant-unorm-snorm-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 0.5; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-unorm-snorm-conversion");
}

TEST_CASE("redundant-unorm-snorm-conversion fix drops the divide",
          "[rules][redundant-unorm-snorm-conversion][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x / 255.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "redundant-unorm-snorm-conversion");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "x");
}
