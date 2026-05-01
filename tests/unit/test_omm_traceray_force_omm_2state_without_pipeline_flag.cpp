// Tests for the omm-traceray-force-omm-2state-without-pipeline-flag rule
// (forward-compatible-stub: cannot read pipeline subobject; surfaces
// RAY_FLAG_FORCE_OMM_2_STATE on TraceRay as "verify").

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_omm_traceray_force_omm_2state_without_pipeline_flag();
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
    rules.push_back(hlsl_clippy::rules::make_omm_traceray_force_omm_2state_without_pipeline_flag());
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

TEST_CASE("omm-traceray-force-omm-2state-without-pipeline-flag fires on TraceRay+force",
          "[rules][omm-traceray-force-omm-2state-without-pipeline-flag]") {
    const std::string hlsl = R"hlsl(
void f() {
    TraceRay(g_BVH, RAY_FLAG_FORCE_OMM_2_STATE | RAY_FLAG_ALLOW_OPACITY_MICROMAPS,
             0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "omm-traceray-force-omm-2state-without-pipeline-flag"));
}

TEST_CASE("omm-traceray-force-omm-2state-without-pipeline-flag silent on plain TraceRay",
          "[rules][omm-traceray-force-omm-2state-without-pipeline-flag]") {
    const std::string hlsl = R"hlsl(
void f() {
    TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "omm-traceray-force-omm-2state-without-pipeline-flag");
    }
}
