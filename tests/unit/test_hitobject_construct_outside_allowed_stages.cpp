// Tests for the hitobject-construct-outside-allowed-stages SER rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_construct_outside_allowed_stages();
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
    rules.push_back(hlsl_clippy::rules::make_hitobject_construct_outside_allowed_stages());
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

TEST_CASE("hitobject-construct-outside-allowed-stages fires on anyhit shader",
          "[rules][hitobject-construct-outside-allowed-stages]") {
    const std::string hlsl = R"hlsl(
[shader("anyhit")]
void OnAnyHit() {
    dx::HitObject hit = HitObject::TraceRay(g_BVH, 0, 0xFF, 0, 1, 0, ray, payload);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "hitobject-construct-outside-allowed-stages"));
}

TEST_CASE("hitobject-construct-outside-allowed-stages does not fire from raygen",
          "[rules][hitobject-construct-outside-allowed-stages]") {
    const std::string hlsl = R"hlsl(
[shader("raygeneration")]
void RayGen() {
    dx::HitObject hit = HitObject::TraceRay(g_BVH, 0, 0xFF, 0, 1, 0, ray, payload);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "hitobject-construct-outside-allowed-stages");
    }
}
