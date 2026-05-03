// Tests for dispatchmesh-grid-too-small-for-wave (Phase 8 v0.8 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_dispatchmesh_grid_too_small_for_wave();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("dmgts.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_dispatchmesh_grid_too_small_for_wave());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
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

TEST_CASE("dispatchmesh-grid-too-small-for-wave fires on (4, 1, 1)",
          "[rules][dispatchmesh-grid-too-small-for-wave]") {
    const std::string hlsl = R"hlsl(
[numthreads(1, 1, 1)]
[shader("amplification")]
void as_main(uint3 dt : SV_DispatchThreadID) {
    DispatchMesh(4, 1, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "dispatchmesh-grid-too-small-for-wave"));
}

TEST_CASE("dispatchmesh-grid-too-small-for-wave silent on (32, 1, 1)",
          "[rules][dispatchmesh-grid-too-small-for-wave]") {
    const std::string hlsl = R"hlsl(
[numthreads(1, 1, 1)]
[shader("amplification")]
void as_main(uint3 dt : SV_DispatchThreadID) {
    DispatchMesh(32, 1, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "dispatchmesh-grid-too-small-for-wave"));
}
