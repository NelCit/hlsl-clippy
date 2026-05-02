// End-to-end tests for the `slang-module-import-without-use` rule
// (ADR 0021 sub-phase C). The rule is Slang-only -- fires on a `.slang`
// source whose `import Foo;` module identifier never appears anywhere
// else in the source, and must stay silent on `.hlsl` sources (HLSL
// has no `import` statement).

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

TEST_CASE("slang-module-import-without-use fires on dead import",
          "[rules][slang-module-import-without-use][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
import Unused;
float test(float x) { return x; }
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-module-import-without-use") == 1U);
}

TEST_CASE("slang-module-import-without-use stays silent when module is referenced",
          "[rules][slang-module-import-without-use][slang]") {
    SourceManager sources;
    const std::string body = R"slang(
import Used;
float test(float x) {
    return Used.helper(x);
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-module-import-without-use") == 0U);
}

TEST_CASE("slang-module-import-without-use does not fire on `.hlsl` source",
          "[rules][slang-module-import-without-use][hlsl-regression]") {
    SourceManager sources;
    // HLSL: even if a comment mentions "import Foo", the rule's
    // grammar gate keeps it silent on `.hlsl` paths.
    const std::string body = R"hlsl(
// import Foo; appears in this comment but the rule is Slang-only.
float squared(float x) { return x * x; }
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    CHECK(count_code(diags, "slang-module-import-without-use") == 0U);
}

TEST_CASE("slang-module-import-without-use fires on disk fixture",
          "[rules][slang-module-import-without-use][slang][fixtures]") {
    const auto path = slang_fixture("dead_import.slang");
    REQUIRE(std::filesystem::exists(path));
    SourceManager sources;
    const auto src = sources.add_file(path);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);
    // One HIT (`Unused`); `Used` is referenced and stays silent.
    CHECK(count_code(diags, "slang-module-import-without-use") == 1U);
}
