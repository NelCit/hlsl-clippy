// End-to-end test: pow-const-squared on the phase 2 fixture.
//
// Phase 0 ships a single rule covering both the `pow-to-mul` (exponents 2/3)
// and `pow-integer-decomposition` (exponents 4/5) HIT markers in the fixture.
// We intentionally accept the `pow-const-squared` rule code for both bands
// (the dedicated rules will land in Phase 1).

#include <algorithm>
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

[[nodiscard]] std::filesystem::path math_fixture() {
    std::filesystem::path p{std::string{hlsl_clippy::test::k_fixtures_dir}};
    p /= "phase2";
    p /= "math.hlsl";
    return p;
}

[[nodiscard]] std::vector<Diagnostic> lint_fixture(const std::filesystem::path& path,
                                                   SourceManager& sources) {
    const auto src = sources.add_file(path);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

}  // namespace

TEST_CASE("pow-const-squared fires on every pow(x, N.0) in math.hlsl", "[rules][pow]") {
    const auto fixture = math_fixture();
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto diagnostics = lint_fixture(fixture, sources);

    // Phase 2 adds additional rules that also fire on math.hlsl; filter to
    // only the pow-const-squared diagnostics for this test.
    std::vector<Diagnostic> pow_diags;
    for (const auto& d : diagnostics) {
        if (d.code == "pow-const-squared")
            pow_diags.push_back(d);
    }

    // The fixture has four firings: pow(1-NdotV, 5.0), pow(x, 2.0), pow(x, 3.0),
    // pow(x, 5.0). pow(2.0, -t*8.0) is excluded because the base is literal 2.
    CHECK(pow_diags.size() >= 4U);
}

TEST_CASE("pow-const-squared does not fire on raw multiplication", "[rules][pow]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float pow_squared(float x) {
    return x * x;
}
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    CHECK(diagnostics.empty());
}

TEST_CASE("pow-const-squared does not fire on pow(2.0, x)", "[rules][pow]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float exp_falloff(float t) {
    return pow(2.0, -t);
}
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    // pow(2.0, x) is `pow-base-two-to-exp2` territory and must not be claimed
    // by pow-const-squared. Phase 2 added pow-base-two-to-exp2 to the default
    // rule pack, so we filter to pow-const-squared specifically rather than
    // expecting the diagnostics list to be entirely empty.
    for (const auto& d : diagnostics) {
        CHECK(d.code != "pow-const-squared");
    }
}

TEST_CASE("pow-const-squared diagnostic resolves to the correct line", "[rules][pow][spans]") {
    const auto fixture = math_fixture();
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto diagnostics = lint_fixture(fixture, sources);
    REQUIRE_FALSE(diagnostics.empty());

    // Phase 2 added pow-base-two-to-exp2 to the default rule pack which also
    // fires on math.hlsl; filter to pow-const-squared specifically before
    // asserting on the line numbers.
    std::vector<unsigned> lines;
    for (const auto& diag : diagnostics) {
        if (diag.code != "pow-const-squared") {
            continue;
        }
        const auto loc = sources.resolve(diag.primary_span.source, diag.primary_span.bytes.lo);
        lines.push_back(loc.line);
    }

    // From the fixture (1-based line numbers):
    //   line  6: pow(1.0 - n_dot_v, 5.0)
    //   line 17: pow(x, 2.0)
    //   line 22: pow(x, 3.0)
    //   line 27: pow(x, 5.0)
    const auto contains = [&](unsigned ln) { return std::ranges::find(lines, ln) != lines.end(); };
    CHECK(contains(6U));
    CHECK(contains(17U));
    CHECK(contains(22U));
    CHECK(contains(27U));

    // pow(2.0, -t * 8.0) at line 12 must NOT be reported by this rule.
    CHECK_FALSE(contains(12U));
}
