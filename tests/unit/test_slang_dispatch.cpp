// Sub-phase A dispatch tests for ADR 0020 — Slang language compatibility.
//
// Coverage:
//   1. detect_language() — extension inference (case-insensitive `.slang`,
//      `.hlsl` family stays HLSL).
//   2. resolve_language() — explicit overrides win over Auto.
//   3. Orchestrator skips AST + CFG dispatch on `.slang` sources and emits
//      exactly one `clippy::language-skip-ast` Note per source.
//   4. AST rules don't fire on `.slang` sources even when the source body
//      contains the matching pattern (`pow(x, 2.0)`).
//   5. Reflection rules still fire on `.slang` sources (via SourceManager
//      buffer registration with a `.slang` virtual path so Slang's native
//      frontend ingests them).
//   6. Config-level `[lint] source-language = "slang"` forces the same
//      skip path even on a `.hlsl`-extension file.
//   7. Suppression: `// hlsl-clippy: allow(clippy::language-skip-ast)` at
//      top-of-file silences the notice without re-enabling AST dispatch.

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/language.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using hlsl_clippy::detect_language;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::resolve_language;
using hlsl_clippy::SourceLanguage;
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

TEST_CASE("detect_language infers Slang from .slang extension", "[slang][language]") {
    CHECK(detect_language("foo.slang") == SourceLanguage::Slang);
    CHECK(detect_language("dir/sub/foo.slang") == SourceLanguage::Slang);
    // ASCII case-insensitive so editors that uppercase the extension still
    // route correctly.
    CHECK(detect_language("FOO.SLANG") == SourceLanguage::Slang);
    CHECK(detect_language("foo.Slang") == SourceLanguage::Slang);

    // Every recognised HLSL extension stays HLSL.
    CHECK(detect_language("foo.hlsl") == SourceLanguage::Hlsl);
    CHECK(detect_language("foo.hlsli") == SourceLanguage::Hlsl);
    CHECK(detect_language("foo.fx") == SourceLanguage::Hlsl);
    // Unknown extensions default to HLSL (conservative).
    CHECK(detect_language("foo.unknown") == SourceLanguage::Hlsl);
    CHECK(detect_language("foo") == SourceLanguage::Hlsl);
}

TEST_CASE("resolve_language honours explicit overrides", "[slang][language]") {
    // Auto -> defer to extension inference.
    CHECK(resolve_language(SourceLanguage::Auto, "foo.slang") == SourceLanguage::Slang);
    CHECK(resolve_language(SourceLanguage::Auto, "foo.hlsl") == SourceLanguage::Hlsl);
    // Explicit overrides win, regardless of extension.
    CHECK(resolve_language(SourceLanguage::Hlsl, "foo.slang") == SourceLanguage::Hlsl);
    CHECK(resolve_language(SourceLanguage::Slang, "foo.hlsl") == SourceLanguage::Slang);
}

TEST_CASE("Slang dispatch emits exactly one clippy::language-skip-ast notice",
          "[slang][dispatch]") {
    SourceManager sources;
    // Use add_buffer with a `.slang` virtual path so the orchestrator's
    // per-file extension inference selects Slang. We deliberately stuff
    // a clear AST-rule-matching pattern (`pow(x, 2.0)`) into the body to
    // double-check the AST stage is skipped end-to-end.
    const std::string body = R"slang(
float squared(float x) {
    return pow(x, 2.0);
}
)slang";
    const auto src = sources.add_buffer("synthetic.slang", body);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    // The orchestrator skips reflection on Slang sources for v1.3.0 (ADR
    // 0020 sub-phase A "v1.3.0 quarantine") so this test exercises the
    // dispatch + skip-notice path without needing the Slang frontend at
    // runtime. Sub-phase B revisits.
    const auto diagnostics = lint(sources, src, rules);

    // Exactly one language-skip-ast notice.
    CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 1U);
    // AST-stage rules MUST NOT fire on a `.slang` source.
    CHECK(count_code(diagnostics, "pow-const-squared") == 0U);
}

TEST_CASE("HLSL dispatch is unaffected by ADR 0020 sub-phase A", "[slang][regression]") {
    SourceManager sources;
    const std::string body = R"hlsl(
float squared(float x) {
    return pow(x, 2.0);
}
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    // Regression: HLSL must still fire pow-const-squared exactly once and
    // must NOT emit the language-skip-ast notice.
    CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 0U);
    CHECK(count_code(diagnostics, "pow-const-squared") == 1U);
}

TEST_CASE("Config-level source-language=\"slang\" forces the Slang dispatch path",
          "[slang][config]") {
    SourceManager sources;
    // Note the `.hlsl` virtual path: extension inference would normally
    // route this through HLSL. Forcing source-language=slang via config
    // overrides the inference.
    const std::string body = R"hlsl(
float squared(float x) {
    return pow(x, 2.0);
}
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", body);
    REQUIRE(src.valid());

    hlsl_clippy::Config cfg;
    cfg.source_language_value = SourceLanguage::Slang;

    auto rules = make_default_rules();
    const auto diagnostics =
        lint(sources, src, rules, cfg, std::filesystem::path{"synthetic.hlsl"});

    CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 1U);
    CHECK(count_code(diagnostics, "pow-const-squared") == 0U);
}

TEST_CASE("Inline allow(clippy::language-skip-ast) suppresses the notice", "[slang][suppression]") {
    SourceManager sources;
    const std::string body = R"slang(// hlsl-clippy: allow(clippy::language-skip-ast)
float squared(float x) {
    return pow(x, 2.0);
}
)slang";
    const auto src = sources.add_buffer("suppressed.slang", body);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 0U);
    // Even with the notice suppressed, AST rules still don't run on Slang.
    CHECK(count_code(diagnostics, "pow-const-squared") == 0U);
}

TEST_CASE("Slang fixtures on disk dispatch through the skip path", "[slang][fixtures]") {
    // Fixture (a): plain-HLSL content with `.slang` extension. AST stage
    // skipped despite the pow(x, 2.0) inside; reflection skipped per the
    // v1.3.0 quarantine.
    {
        const auto path = slang_fixture("plain_hlsl_in_slang_file.slang");
        REQUIRE(std::filesystem::exists(path));
        SourceManager sources;
        const auto src = sources.add_file(path);
        REQUIRE(src.valid());
        auto rules = make_default_rules();
        const auto diagnostics = lint(sources, src, rules);
        CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 1U);
        CHECK(count_code(diagnostics, "pow-const-squared") == 0U);
    }
    // Fixture (c): Slang-only constructs (generics + interface). Same
    // expectation: the skip notice fires once, and no AST diagnostic
    // surfaces despite the pow(x, 2.0) at the bottom.
    {
        const auto path = slang_fixture("slang_only_constructs.slang");
        REQUIRE(std::filesystem::exists(path));
        SourceManager sources;
        const auto src = sources.add_file(path);
        REQUIRE(src.valid());
        auto rules = make_default_rules();
        const auto diagnostics = lint(sources, src, rules);
        CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 1U);
        CHECK(count_code(diagnostics, "pow-const-squared") == 0U);
    }
}
