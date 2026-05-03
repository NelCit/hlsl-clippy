// Tests for the hitobject-invoke-after-recursion-cap SER rule (Phase 3
// forward-compatible-stub: catches the closesthit + Invoke pattern, with
// precise depth budget deferred to Phase 4).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_invoke_after_recursion_cap();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_hitobject_invoke_after_recursion_cap());
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

TEST_CASE("hitobject-invoke-after-recursion-cap fires on closesthit Invoke",
          "[rules][hitobject-invoke-after-recursion-cap]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void OnHit() {
    dx::HitObject deeper = HitObject::FromRayQuery(q);
    deeper.Invoke(p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "hitobject-invoke-after-recursion-cap"));
}

TEST_CASE("hitobject-invoke-after-recursion-cap does not fire from raygen",
          "[rules][hitobject-invoke-after-recursion-cap]") {
    const std::string hlsl = R"hlsl(
[shader("raygeneration")]
void RayGen() {
    dx::HitObject hit = HitObject::TraceRay(g_BVH, 0, 0xFF, 0, 1, 0, ray, payload);
    hit.Invoke(payload);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "hitobject-invoke-after-recursion-cap");
    }
}
