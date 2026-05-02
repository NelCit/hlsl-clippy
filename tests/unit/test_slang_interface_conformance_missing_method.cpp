// End-to-end tests for the `slang-interface-conformance-missing-method`
// rule (ADR 0021 sub-phase C). The rule is Slang-only -- fires on a
// `.slang` source whose `extension Foo : IFoo {}` body does not
// implement every method declared on the interface, and must stay
// silent on `.hlsl` sources (HLSL has no `extension` / `interface`
// constructs in this form).

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

TEST_CASE("slang-interface-conformance-missing-method fires on empty extension body",
          "[rules][slang-interface-conformance-missing-method][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
interface IFoo {
    float compute(float x);
    float reduce(float a, float b);
}
struct BadImpl { }
extension BadImpl : IFoo {
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    // One diagnostic per missing method -> two firings.
    CHECK(count_code(diags, "slang-interface-conformance-missing-method") == 2U);
}

TEST_CASE("slang-interface-conformance-missing-method silent when all methods implemented",
          "[rules][slang-interface-conformance-missing-method][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
interface IFoo {
    float compute(float x);
}
struct GoodImpl { }
extension GoodImpl : IFoo {
    float compute(float x) { return x; }
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-interface-conformance-missing-method") == 0U);
}

TEST_CASE("slang-interface-conformance-missing-method does not fire on `.hlsl` source",
          "[rules][slang-interface-conformance-missing-method][hlsl-regression]") {
    SourceManager sources;
    // The `interface` / `extension` keywords parsed via tree-sitter-hlsl
    // would not produce `interface_specifier` / `extension_specifier`
    // nodes; the rule's grammar gate ensures it stays silent here.
    const std::string body = R"hlsl(
struct Foo {
    float compute(float x) { return x; }
};
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-interface-conformance-missing-method") == 0U);
}

TEST_CASE("slang-interface-conformance-missing-method fires on disk fixture",
          "[rules][slang-interface-conformance-missing-method][slang][fixtures]") {
    const auto path = slang_fixture("interface_conformance_missing.slang");
    REQUIRE(std::filesystem::exists(path));
    SourceManager sources;
    const auto src = sources.add_file(path);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    // BadImpl extension is missing two methods; GoodImpl is complete.
    CHECK(count_code(diags, "slang-interface-conformance-missing-method") == 2U);
}
