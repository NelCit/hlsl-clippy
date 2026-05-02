// Tests for oriented-bbox-not-set-on-rdna4 (Phase 8 deferred; ADR 0018).

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_oriented_bbox_not_set_on_rdna4();
}

namespace {

using hlsl_clippy::Config;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::ExperimentalTarget;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_under(const std::string& hlsl,
                                                 ExperimentalTarget target) {
    SourceManager sources;
    const auto src = sources.add_buffer("obbox.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_oriented_bbox_not_set_on_rdna4());
    Config cfg{};
    cfg.experimental_target_value = target;
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, cfg, std::filesystem::path{"obbox.hlsl"}, opts);
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

TEST_CASE("oriented-bbox-not-set-on-rdna4 fires when RT call is present and target = Rdna4",
          "[rules][oriented-bbox-not-set-on-rdna4]") {
    const std::string hlsl = R"hlsl(
[shader("raygeneration")]
void rg() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK(has_rule(lint_under(hlsl, ExperimentalTarget::Rdna4),
                   "oriented-bbox-not-set-on-rdna4"));
}

TEST_CASE("oriented-bbox-not-set-on-rdna4 silent when no RT call is present",
          "[rules][oriented-bbox-not-set-on-rdna4]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target { return 0; }
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::Rdna4),
                         "oriented-bbox-not-set-on-rdna4"));
}

TEST_CASE("oriented-bbox-not-set-on-rdna4 silent under default config",
          "[rules][oriented-bbox-not-set-on-rdna4][experimental]") {
    const std::string hlsl = R"hlsl(
[shader("raygeneration")]
void rg() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::None),
                         "oriented-bbox-not-set-on-rdna4"));
}
