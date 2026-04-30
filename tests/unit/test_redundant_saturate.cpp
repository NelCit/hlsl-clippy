// End-to-end test: redundant-saturate on synthesized HLSL and the phase 2
// fixture. Phase 1 only matches the direct lexical nest (`saturate(saturate
// (...))`); the split-variable form is documented as a follow-up.

#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::filesystem::path redundant_fixture() {
    std::filesystem::path p{std::string{hlsl_clippy::test::k_fixtures_dir}};
    p /= "phase2";
    p /= "redundant.hlsl";
    return p;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

}  // namespace

TEST_CASE("redundant-saturate fires on saturate(saturate(x))", "[rules][redundant-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 c) {
    return saturate(saturate(c));
}
)hlsl";
    const auto diagnostics = lint_buffer(hlsl, sources);

    bool found = false;
    for (const auto& d : diagnostics) {
        if (d.code == "redundant-saturate") {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("redundant-saturate does not fire on a single saturate", "[rules][redundant-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 c) {
    return saturate(c);
}
)hlsl";
    const auto diagnostics = lint_buffer(hlsl, sources);

    for (const auto& d : diagnostics) {
        CHECK(d.code != "redundant-saturate");
    }
}

TEST_CASE("redundant-saturate does not fire on saturate(other_call(x))",
          "[rules][redundant-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 c) {
    return saturate(normalize(c));
}
)hlsl";
    const auto diagnostics = lint_buffer(hlsl, sources);

    for (const auto& d : diagnostics) {
        CHECK(d.code != "redundant-saturate");
    }
}

TEST_CASE("redundant-saturate fires on the phase 2 fixture nested case",
          "[rules][redundant-saturate]") {
    const auto fixture = redundant_fixture();
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    std::vector<unsigned> hit_lines;
    for (const auto& d : diagnostics) {
        if (d.code == "redundant-saturate") {
            const auto loc = sources.resolve(d.primary_span.source, d.primary_span.bytes.lo);
            hit_lines.push_back(loc.line);
        }
    }
    // The fixture's direct-nested case is on line 5 (`return saturate(saturate(c))`).
    const auto contains = [&](unsigned ln) {
        return std::ranges::find(hit_lines, ln) != hit_lines.end();
    };
    CHECK(contains(5U));
}

TEST_CASE("redundant-saturate carries a machine-applicable fix",
          "[rules][redundant-saturate][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 c) { return saturate(saturate(c)); }
)hlsl";
    const auto diagnostics = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diagnostics) {
        if (d.code == "redundant-saturate") {
            hit = &d;
            break;
        }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    // The replacement is exactly the inner call's text.
    CHECK(hit->fixes[0].edits[0].replacement == "saturate(c)");
}

TEST_CASE("inline allow(redundant-saturate) suppresses the diagnostic",
          "[rules][redundant-saturate][suppress]") {
    SourceManager sources;
    const std::string hlsl =
        "// hlsl-clippy: allow(redundant-saturate)\n"
        "float3 f(float3 c) { return saturate(saturate(c)); }\n";
    const auto diagnostics = lint_buffer(hlsl, sources);

    for (const auto& d : diagnostics) {
        CHECK(d.code != "redundant-saturate");
    }
}
