// Unit tests for the `[experimental] target` config surface (ADR 0018).
//
// Coverage:
//   - Default `Config{}` reports `experimental_target() == None`.
//   - `[experimental] target = "rdna4"` parses to `Rdna4`.
//   - Unrecognised value (`target = "ada"`) falls back to `None` and
//     surfaces a single-line warning in `Config::warnings`.
//   - A spy rule overriding `experimental_target()` to `Rdna4` fires under
//     a `Rdna4`-targeted config and is silently skipped under a default
//     (`None`-targeted) config.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

// `AstTree` is defined in the private parser-internal header; tests already
// have `core/src` on the include path so they can pull this in.
#include "parser_internal.hpp"

namespace {

using hlsl_clippy::AstCursor;
using hlsl_clippy::AstTree;
using hlsl_clippy::Config;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::ExperimentalTarget;
using hlsl_clippy::Rule;
using hlsl_clippy::RuleContext;
using hlsl_clippy::Severity;
using hlsl_clippy::SourceManager;
using hlsl_clippy::Span;
using hlsl_clippy::Stage;

/// Spy rule that only fires under `experimental_target = Rdna4`. Emits a
/// single fixed-id diagnostic on the whole-tree pass; the diagnostic count
/// in the test reflects whether the orchestrator dispatched the rule.
class Rdna4SpyRule final : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return "test::experimental-rdna4-spy";
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return "test";
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept override {
        return ExperimentalTarget::Rdna4;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        Diagnostic diag;
        diag.code = std::string{id()};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{tree.source_id(), hlsl_clippy::ByteSpan{0U, 0U}};
        diag.message = "spy fired";
        ctx.emit(std::move(diag));
    }
};

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_spy_pack() {
    std::vector<std::unique_ptr<Rule>> v;
    v.push_back(std::make_unique<Rdna4SpyRule>());
    return v;
}

[[nodiscard]] std::size_t count_spy_diagnostics(const std::vector<Diagnostic>& diags) noexcept {
    std::size_t count = 0;
    for (const auto& d : diags) {
        if (d.code == "test::experimental-rdna4-spy") {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("Default Config reports experimental_target() == None", "[config][experimental]") {
    const Config cfg{};
    CHECK(cfg.experimental_target() == ExperimentalTarget::None);
    CHECK(cfg.warnings.empty());

    // Loading an empty TOML string also produces `None` and no warnings.
    const auto result = hlsl_clippy::load_config_string("");
    REQUIRE(result.has_value());
    CHECK(result.value().experimental_target() == ExperimentalTarget::None);
    CHECK(result.value().warnings.empty());
}

TEST_CASE("[experimental] target = \"rdna4\" parses to Rdna4", "[config][experimental]") {
    constexpr std::string_view k_toml = R"(
[experimental]
target = "rdna4"
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE(result.has_value());
    const auto& cfg = result.value();
    CHECK(cfg.experimental_target() == ExperimentalTarget::Rdna4);
    CHECK(cfg.warnings.empty());

    // The other recognised tokens round-trip too.
    constexpr std::string_view k_blackwell = R"(
[experimental]
target = "blackwell"
)";
    const auto bw = hlsl_clippy::load_config_string(k_blackwell);
    REQUIRE(bw.has_value());
    CHECK(bw.value().experimental_target() == ExperimentalTarget::Blackwell);

    constexpr std::string_view k_xe2 = R"(
[experimental]
target = "xe2"
)";
    const auto xe2 = hlsl_clippy::load_config_string(k_xe2);
    REQUIRE(xe2.has_value());
    CHECK(xe2.value().experimental_target() == ExperimentalTarget::Xe2);
}

TEST_CASE("Unrecognised target falls back to None and emits a warning", "[config][experimental]") {
    constexpr std::string_view k_toml = R"(
[experimental]
target = "ada"
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE(result.has_value());
    const auto& cfg = result.value();
    CHECK(cfg.experimental_target() == ExperimentalTarget::None);
    REQUIRE(cfg.warnings.size() == 1U);
    CHECK(cfg.warnings.front().find("ada") != std::string::npos);
    CHECK(cfg.warnings.front().find("experimental.target") != std::string::npos);

    // Empty string is treated as "not selected" and does not warn.
    constexpr std::string_view k_empty = R"(
[experimental]
target = ""
)";
    const auto empty_res = hlsl_clippy::load_config_string(k_empty);
    REQUIRE(empty_res.has_value());
    CHECK(empty_res.value().experimental_target() == ExperimentalTarget::None);
    CHECK(empty_res.value().warnings.empty());
}

TEST_CASE("Spy rule fires only when its experimental target matches the active config",
          "[config][experimental][orchestrator]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    const auto src = sources.add_buffer("experimental_spy.hlsl", hlsl);
    REQUIRE(src.valid());

    auto rules = make_spy_pack();

    // Default config: spy rule must NOT fire (experimental_target = Rdna4
    // but config target = None).
    {
        const Config cfg{};
        const auto diags = hlsl_clippy::lint(
            sources, src, rules, cfg, std::filesystem::path{"experimental_spy.hlsl"});
        CHECK(count_spy_diagnostics(diags) == 0U);
    }

    // Rdna4 config: spy rule fires.
    {
        Config cfg{};
        cfg.experimental_target_value = ExperimentalTarget::Rdna4;
        const auto diags = hlsl_clippy::lint(
            sources, src, rules, cfg, std::filesystem::path{"experimental_spy.hlsl"});
        CHECK(count_spy_diagnostics(diags) == 1U);
    }

    // Mismatched non-None target (Blackwell): spy rule still does not fire.
    {
        Config cfg{};
        cfg.experimental_target_value = ExperimentalTarget::Blackwell;
        const auto diags = hlsl_clippy::lint(
            sources, src, rules, cfg, std::filesystem::path{"experimental_spy.hlsl"});
        CHECK(count_spy_diagnostics(diags) == 0U);
    }

    // No-config overload: experimental rules are gated off (active = None).
    {
        const auto diags = hlsl_clippy::lint(sources, src, rules);
        CHECK(count_spy_diagnostics(diags) == 0U);
    }
}
