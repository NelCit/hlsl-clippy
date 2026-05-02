// Tests for cluster-id-without-cluster-geometry-feature-check (Phase 8 v0.10 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_cluster_id_without_cluster_geometry_feature_check();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  const std::string& profile) {
    SourceManager sources;
    const auto src = sources.add_buffer("ciwcg.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(
        hlsl_clippy::rules::make_cluster_id_without_cluster_geometry_feature_check());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = profile;
    return lint(sources, src, rules, opts);
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

TEST_CASE("cluster-id-without-cluster-geometry-feature-check fires on SM 6.10 without guard",
          "[rules][cluster-id-without-cluster-geometry-feature-check]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    uint id = ClusterID();
    p.x = id;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                   "cluster-id-without-cluster-geometry-feature-check"));
}

TEST_CASE("cluster-id-without-cluster-geometry-feature-check silent with guard",
          "[rules][cluster-id-without-cluster-geometry-feature-check]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    if (IsClusteredGeometrySupported()) {
        uint id = ClusterID();
        p.x = id;
    }
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                         "cluster-id-without-cluster-geometry-feature-check"));
}

TEST_CASE("cluster-id-without-cluster-geometry-feature-check silent without ClusterID call",
          "[rules][cluster-id-without-cluster-geometry-feature-check]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target { return 0; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                         "cluster-id-without-cluster-geometry-feature-check"));
}
