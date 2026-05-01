// End-to-end tests for the redundant-precision-cast rule.
// Detects same-target nested casts: (T)((T)x) -> (T)x. Machine-applicable.

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

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags,
                            std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

// ---- positive cases ----

TEST_CASE("redundant-precision-cast fires on (float)((float)x)",
          "[rules][redundant-precision-cast]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return (float)((float)x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-precision-cast"));
}

TEST_CASE("redundant-precision-cast fires on (int)((int)y)",
          "[rules][redundant-precision-cast]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
int f(int y) { return (int)((int)y); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-precision-cast"));
}

TEST_CASE("redundant-precision-cast fires on (uint)((uint)z)",
          "[rules][redundant-precision-cast]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint z) { return (uint)((uint)z); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-precision-cast"));
}

// ---- negative cases ----

TEST_CASE("redundant-precision-cast does not fire on a single (float)x",
          "[rules][redundant-precision-cast]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(int x) { return (float)x; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "redundant-precision-cast");
    }
}

TEST_CASE("redundant-precision-cast does not fire on (float)((int)x)",
          "[rules][redundant-precision-cast]") {
    // Cross-type casts may change semantics; this rule only flags
    // identical-target nesting and must stay silent here.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return (float)((int)x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "redundant-precision-cast");
    }
}

TEST_CASE("redundant-precision-cast does not fire on (int)((float)x)",
          "[rules][redundant-precision-cast]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
int f(int x) { return (int)((float)x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "redundant-precision-cast");
    }
}

// ---- fix applicability ----

TEST_CASE("redundant-precision-cast fix is machine-applicable",
          "[rules][redundant-precision-cast][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return (float)((float)x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "redundant-precision-cast") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    // The replacement should be the inner cast verbatim — i.e. it should still
    // start with `(float)` (the inner cast text), not be empty or unrelated.
    const auto& replacement = hit->fixes[0].edits[0].replacement;
    CHECK(replacement.find("(float)") != std::string::npos);
}
