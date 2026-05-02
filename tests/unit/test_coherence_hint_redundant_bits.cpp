// Tests for the coherence-hint-redundant-bits rule (Phase 4 Pack E).
// Forward-compatible-stub: fires on the SER-spec ceiling violation
// (`hintBits` literal > 32) until the bit-range domain lands.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coherence_hint_redundant_bits();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_coherence_hint_redundant_bits());
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

TEST_CASE("coherence-hint-redundant-bits fires when hintBits exceeds the SER ceiling",
          "[rules][coherence-hint-redundant-bits]") {
    const std::string hlsl = R"hlsl(
void f(dx::HitObject hit, uint hg) {
    dx::MaybeReorderThread(hit, hg, 64);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coherence-hint-redundant-bits"));
}

TEST_CASE("coherence-hint-redundant-bits is silent on a tight 4-bit hint",
          "[rules][coherence-hint-redundant-bits]") {
    const std::string hlsl = R"hlsl(
void f(dx::HitObject hit, uint hg) {
    dx::MaybeReorderThread(hit, hg, 4);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coherence-hint-redundant-bits");
    }
}

TEST_CASE("coherence-hint-redundant-bits is silent at the spec ceiling itself",
          "[rules][coherence-hint-redundant-bits]") {
    const std::string hlsl = R"hlsl(
void f(dx::HitObject hit, uint hg) {
    dx::MaybeReorderThread(hit, hg, 32);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coherence-hint-redundant-bits");
    }
}

TEST_CASE("coherence-hint-redundant-bits attaches a clamp-to-32 Fix",
          "[rules][coherence-hint-redundant-bits][fix]") {
    // Regression for v0.6.8: rule was tagged `applicability: machine-applicable`
    // in docs but emitted a Fix-less diagnostic. The over-ceiling literal is
    // ignored by the SER scheduler at runtime, so clamping the source literal
    // to 32 is semantics-preserving.
    const std::string hlsl = R"hlsl(
void f(dx::HitObject hit, uint hg) {
    dx::MaybeReorderThread(hit, hg, 64);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "coherence-hint-redundant-bits") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement == "32");
        // The edit targets the third-argument span, which is the same as the
        // primary diagnostic span.
        CHECK(fix.edits.front().span.bytes.lo == d.primary_span.bytes.lo);
        CHECK(fix.edits.front().span.bytes.hi == d.primary_span.bytes.hi);
    }
    CHECK(saw);
}
