// End-to-end tests for the countbits-vs-manual-popcount rule.
// Detects the textbook open-coded popcount loop; suggestion-only fix.

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

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

// ---- positive cases ----

TEST_CASE("countbits-vs-manual-popcount fires on the textbook for-loop popcount",
          "[rules][countbits-vs-manual-popcount]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint popcount(uint x) {
    uint count = 0;
    for (int i = 0; i < 32; ++i) {
        count += (x & 1);
        x >>= 1;
    }
    return count;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "countbits-vs-manual-popcount"));
}

TEST_CASE("countbits-vs-manual-popcount fires on while-loop popcount",
          "[rules][countbits-vs-manual-popcount]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint popcount(uint x) {
    uint count = 0;
    while (x != 0) {
        count += x & 1;
        x = x >> 1;
    }
    return count;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "countbits-vs-manual-popcount"));
}

// ---- negative cases ----

TEST_CASE("countbits-vs-manual-popcount does not fire on a counting loop without a shift",
          "[rules][countbits-vs-manual-popcount]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint sum_low_bits(uint x) {
    uint count = 0;
    for (int i = 0; i < 32; ++i) {
        count += (x & 1);
    }
    return count;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "countbits-vs-manual-popcount");
}

TEST_CASE("countbits-vs-manual-popcount does not fire on a shift-only loop",
          "[rules][countbits-vs-manual-popcount]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint shifty(uint x) {
    uint y = x;
    for (int i = 0; i < 4; ++i) {
        y >>= 1;
    }
    return y;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "countbits-vs-manual-popcount");
}

TEST_CASE("countbits-vs-manual-popcount does not fire on the countbits intrinsic itself",
          "[rules][countbits-vs-manual-popcount]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return countbits(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "countbits-vs-manual-popcount");
}

// ---- fix applicability ----

TEST_CASE("countbits-vs-manual-popcount fix is suggestion-only",
          "[rules][countbits-vs-manual-popcount][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint popcount(uint x) {
    uint count = 0;
    for (int i = 0; i < 32; ++i) {
        count += (x & 1);
        x >>= 1;
    }
    return count;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "countbits-vs-manual-popcount") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}
