// Tests for ser-coherence-hint-bits-overflow (Phase 8 v0.9 pack; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_ser_coherence_hint_bits_overflow();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("schbo.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_ser_coherence_hint_bits_overflow());
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return &d;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("ser-coherence-hint-bits-overflow fires on bits=24 (> 16)",
          "[rules][ser-coherence-hint-bits-overflow]") {
    const std::string hlsl = R"hlsl(
void f(uint hint) {
    MaybeReorderThread(hint, 24);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "ser-coherence-hint-bits-overflow"));
}

TEST_CASE("ser-coherence-hint-bits-overflow silent on bits=8",
          "[rules][ser-coherence-hint-bits-overflow]") {
    const std::string hlsl = R"hlsl(
void f(uint hint) {
    MaybeReorderThread(hint, 8);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "ser-coherence-hint-bits-overflow"));
}

TEST_CASE("ser-coherence-hint-bits-overflow fix clamps the literal to the cap",
          "[rules][ser-coherence-hint-bits-overflow][fix]") {
    const std::string hlsl = R"hlsl(
void f(uint hint) {
    MaybeReorderThread(hint, 24);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    const auto* hit = find_rule(diags, "ser-coherence-hint-bits-overflow");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->fixes.size() == 1U);
    // The arg is already a literal -- no side effects to repeat. SER masks
    // values above the cap at runtime, so the rewrite is bit-identical.
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "16");
}
