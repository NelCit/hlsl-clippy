// Tests for the coherence-hint-encodes-shader-type rule (Phase 4 Pack E).
// Forward-compatible-stub: detects taint via textual co-occurrence of
// `GetShaderTableIndex` / `IsHit` in the enclosing function until the full
// taint analyzer lands.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coherence_hint_encodes_shader_type();
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
    rules.push_back(hlsl_clippy::rules::make_coherence_hint_encodes_shader_type());
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

TEST_CASE("coherence-hint-encodes-shader-type fires on direct GetShaderTableIndex use",
          "[rules][coherence-hint-encodes-shader-type]") {
    const std::string hlsl = R"hlsl(
void RayGen(dx::HitObject hit) {
    dx::MaybeReorderThread(hit, hit.GetShaderTableIndex(), 4);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coherence-hint-encodes-shader-type"));
}

TEST_CASE("coherence-hint-encodes-shader-type fires on indirect (assigned-then-used) taint",
          "[rules][coherence-hint-encodes-shader-type]") {
    const std::string hlsl = R"hlsl(
void RayGen(dx::HitObject hit) {
    uint hg = hit.GetShaderTableIndex();
    dx::MaybeReorderThread(hit, hg, 4);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coherence-hint-encodes-shader-type"));
}

TEST_CASE("coherence-hint-encodes-shader-type is silent on application-specific axis",
          "[rules][coherence-hint-encodes-shader-type]") {
    const std::string hlsl = R"hlsl(
void RayGen(dx::HitObject hit) {
    uint matId = LookupMaterialId(hit);
    dx::MaybeReorderThread(hit, matId, 6);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coherence-hint-encodes-shader-type");
    }
}
