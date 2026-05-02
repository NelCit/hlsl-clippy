// Sub-phase A dispatch tests for ADR 0020 — Slang language compatibility.
// v1.3.1 (ADR 0020 sub-phase A "Risks & mitigations") expanded the surface
// to verify the reflection-bridge fix lights up Stage::Reflection rules on
// `.slang`.
//
// Coverage:
//   1. detect_language() — extension inference (case-insensitive `.slang`,
//      `.hlsl` family stays HLSL).
//   2. resolve_language() — explicit overrides win over Auto.
//   3. Orchestrator skips AST + CFG + IR dispatch on `.slang` sources and
//      emits exactly one `clippy::language-skip-ast` Note per source.
//   4. AST rules don't fire on `.slang` sources even when the source body
//      contains the matching pattern (`pow(x, 2.0)`).
//   5. Bridge regression (v1.3.1): direct `ReflectionEngine::reflect` on a
//      `.slang` source returns successfully (pre-fix this segfaulted inside
//      Slang's frontend due to the call-suffix corrupting the path's
//      extension).
//   6. End-to-end (v1.3.1): a Stage::Reflection rule (`oversized-cbuffer`)
//      fires through `lint()` + `make_default_rules()` on a `.slang`
//      source containing a large cbuffer. v1.3.0's reflection quarantine
//      had silently suppressed this.
//   7. Config-level `[lint] source-language = "slang"` forces the same
//      skip path even on a `.hlsl`-extension file.
//   8. Suppression: `// hlsl-clippy: allow(clippy::language-skip-ast)` at
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
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "reflection/engine.hpp"

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

// v1.3.1 (ADR 0020 sub-phase A "Risks & mitigations"): direct bridge repro.
// Verifies that the SlangBridge can ingest a `.slang`-extensioned source
// without crashing inside Slang's frontend. Pre-fix this segfaulted in
// `loadModuleFromSourceString` because the bridge's per-call uniquification
// suffix corrupted the path's extension (`foo.slang__7`), which Slang's
// source-language inference could not classify. The v1.3.1 fix splices the
// suffix BEFORE the extension (`foo__7.slang`) so Slang still classifies
// the path as a Slang source and routes it through its native frontend.
TEST_CASE("ReflectionEngine reflects a `.slang` source without crashing",
          "[slang][bridge][regression]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    // Plain HLSL/Slang-compatible body — Slang's `.slang` frontend is a
    // superset of HLSL so a cbuffer + a [shader] entry point ingest cleanly.
    const std::string body = R"slang(
cbuffer SceneConstants {
    float4x4 view_proj;
    float    time;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return float4(uv, 0.0, 1.0);
}
)slang";
    const auto src = sources.add_buffer("synthetic_v131.slang", body);
    REQUIRE(src.valid());

    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    REQUIRE(result.has_value());
    const auto& info = result.value();
    CHECK(info.target_profile == "sm_6_6");
    CHECK(info.cbuffers.size() >= 1U);
    CHECK(info.entry_points.size() >= 1U);
}

// Regression repro for the unit test that was crashing: minimal `.slang`
// content with no entry points, just a free function. Pre-fix this would
// segfault inside Slang's frontend when called via the orchestrator's
// reflection-stage dispatch on `.slang` sources.
TEST_CASE("ReflectionEngine reflects a `.slang` source containing only a free function",
          "[slang][bridge][regression]") {
    auto& engine = hlsl_clippy::reflection::ReflectionEngine::instance();
    engine.clear_cache();

    SourceManager sources;
    const std::string body = R"slang(
float squared(float x) {
    return pow(x, 2.0);
}
)slang";
    const auto src = sources.add_buffer("free_func.slang", body);
    REQUIRE(src.valid());

    // We don't care what reflection returns for a entry-point-free source —
    // we only care that the call returns at all rather than crashing.
    const auto result = engine.reflect(sources, src, std::string_view{"sm_6_6"});
    // Slang may either succeed with an empty layout or surface a soft
    // diagnostic; both outcomes are acceptable here. The crash regression
    // is the only thing this test guards.
    if (!result.has_value()) {
        CHECK(result.error().code == "clippy::reflection");
    }
}

// v1.3.1 (ADR 0020 sub-phase A "Risks & mitigations"): canonical
// Stage::Reflection rule fires on a `.slang` source. `oversized-cbuffer`
// is a textbook reflection-only rule that consumes the bridge's
// `CBufferLayout::total_bytes` field directly — no AST/CFG dependency.
// We construct a cbuffer larger than the 4096-byte threshold and assert
// the rule fires the diagnostic that v1.3.0's reflection quarantine
// was suppressing.
TEST_CASE("Stage::Reflection rule fires on a `.slang` source with a large cbuffer",
          "[slang][bridge][reflection-rule]") {
    SourceManager sources;
    // 512 float4s = 8192 bytes, well over the 4 KB threshold the
    // `oversized-cbuffer` rule guards. Slang's `.slang` frontend ingests
    // this cleanly and surfaces the same `CBufferLayout` shape as on HLSL.
    const std::string body = R"slang(
struct BulkConstants {
    float4 entries[512];
};

ConstantBuffer<BulkConstants> bulk;

[shader("pixel")]
float4 ps_main() : SV_Target {
    return bulk.entries[0];
}
)slang";
    const auto src = sources.add_buffer("oversized_cbuf.slang", body);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    hlsl_clippy::LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    const auto diagnostics = lint(sources, src, rules, options);

    // language-skip-ast still fires (AST/CFG/IR remain gated); the
    // reflection rule fires because the bridge can now ingest `.slang`.
    CHECK(count_code(diagnostics, "clippy::language-skip-ast") == 1U);
    CHECK(count_code(diagnostics, "oversized-cbuffer") >= 1U);
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
    // v1.3.1 (ADR 0020 sub-phase A "Risks & mitigations"): reflection-stage
    // rules now fire on `.slang` sources; AST/CFG/IR remain gated. This
    // test still asserts the dispatch + skip-notice surface — the AST rule
    // (`pow-const-squared`) MUST NOT fire because we haven't parsed the
    // source through tree-sitter-hlsl. Reflection on a body without
    // resources or entry points is a no-op so we don't assert anything
    // additional here; the dedicated `[reflection-rule]` test above
    // verifies the lit-up surface end-to-end.
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
    // skipped despite the pow(x, 2.0) inside; reflection now runs (v1.3.1)
    // but no reflection-stage rule fires on a body without resources.
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
