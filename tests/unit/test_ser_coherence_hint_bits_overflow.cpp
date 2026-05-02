// Tests for ser-coherence-hint-bits-overflow (Phase 8 v0.9 pack; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_ser_coherence_hint_bits_overflow();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("schbo.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_ser_coherence_hint_bits_overflow());
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
