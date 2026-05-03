// End-to-end tests for the `slang-associatedtype-shadowing-builtin`
// rule (ADR 0021 sub-phase C). The rule is Slang-only -- fires on a
// `.slang` source whose `associatedtype X;` collides with a built-in
// HLSL/Slang type name, and must stay silent on `.hlsl` sources (HLSL
// has no `associatedtype` keyword).

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::size_t count_code(const std::vector<Diagnostic>& diags, std::string_view code) {
    return static_cast<std::size_t>(std::count_if(
        diags.begin(), diags.end(), [code](const Diagnostic& d) { return d.code == code; }));
}

[[nodiscard]] std::filesystem::path slang_fixture(std::string_view name) {
    std::filesystem::path p{std::string{shader_clippy::test::k_fixtures_dir}};
    p /= "slang";
    p /= std::string{name};
    return p;
}

}  // namespace

TEST_CASE("slang-associatedtype-shadowing-builtin fires on Texture2D shadow",
          "[rules][slang-associatedtype-shadowing-builtin][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
interface IBad {
    associatedtype Texture2D;
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-associatedtype-shadowing-builtin") == 1U);
}

TEST_CASE("slang-associatedtype-shadowing-builtin stays silent on non-builtin name",
          "[rules][slang-associatedtype-shadowing-builtin][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
interface ISafe {
    associatedtype Differential;
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-associatedtype-shadowing-builtin") == 0U);
}

TEST_CASE("slang-associatedtype-shadowing-builtin does not fire on `.hlsl` source",
          "[rules][slang-associatedtype-shadowing-builtin][hlsl-regression]") {
    SourceManager sources;
    // HLSL has no `associatedtype` keyword. Even if a comment mentions
    // it, the rule's grammar gate keeps it silent on `.hlsl` paths.
    const std::string body = R"hlsl(
// associatedtype Texture2D appears in this comment but the rule is Slang-only.
struct Foo { };
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-associatedtype-shadowing-builtin") == 0U);
}

TEST_CASE("slang-associatedtype-shadowing-builtin fires on disk fixture",
          "[rules][slang-associatedtype-shadowing-builtin][slang][fixtures]") {
    const auto path = slang_fixture("associatedtype_shadow.slang");
    REQUIRE(std::filesystem::exists(path));
    SourceManager sources;
    const auto src = sources.add_file(path);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    // Two HITs: Texture2D + Buffer. Differential stays silent.
    CHECK(count_code(diags, "slang-associatedtype-shadowing-builtin") == 2U);
}
