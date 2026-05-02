// End-to-end tests for the `slang-generic-without-constraint` rule
// (ADR 0021 sub-phase C). The rule is Slang-only -- it must fire on a
// `.slang` source whose `__generic<T>` has no interface-conformance
// bound, and must stay silent on `.hlsl` sources containing similar
// HLSL constructs (HLSL has no `__generic` keyword).

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
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

[[nodiscard]] std::size_t count_code(const std::vector<Diagnostic>& diags, std::string_view code) {
    return static_cast<std::size_t>(std::count_if(
        diags.begin(), diags.end(), [code](const Diagnostic& d) { return d.code == code; }));
}

[[nodiscard]] std::filesystem::path slang_fixture(std::string_view name) {
    std::filesystem::path p{std::string{hlsl_clippy::test::k_fixtures_dir}};
    p /= "slang";
    p /= std::string{name};
    return p;
}

}  // namespace

TEST_CASE("slang-generic-without-constraint fires on `__generic<T>` with no bound",
          "[rules][slang-generic-without-constraint][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
__generic<T>
void f(T x) { }
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-generic-without-constraint") == 1U);
}

TEST_CASE("slang-generic-without-constraint stays silent on constrained `__generic<T : ICompute>`",
          "[rules][slang-generic-without-constraint][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
interface ICompute { float compute(float x); }
__generic<T : ICompute>
void g(T x) { }
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-generic-without-constraint") == 0U);
}

TEST_CASE("slang-generic-without-constraint does not fire on `.hlsl` (Slang-only rule)",
          "[rules][slang-generic-without-constraint][hlsl-regression]") {
    SourceManager sources;
    // HLSL has no `__generic` keyword, but even if a source contained
    // a similar string (e.g. inside a comment) the rule must not fire
    // through the tree-sitter-hlsl AST.
    const std::string body = R"hlsl(
// __generic<T> appears here in a comment but the rule must stay silent.
float squared(float x) { return x * x; }
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-generic-without-constraint") == 0U);
}

TEST_CASE("slang-generic-without-constraint fires on disk fixture",
          "[rules][slang-generic-without-constraint][slang][fixtures]") {
    const auto path = slang_fixture("generic_unconstrained.slang");
    REQUIRE(std::filesystem::exists(path));
    SourceManager sources;
    const auto src = sources.add_file(path);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    // Exactly one HIT in the fixture body.
    CHECK(count_code(diags, "slang-generic-without-constraint") == 1U);
}
