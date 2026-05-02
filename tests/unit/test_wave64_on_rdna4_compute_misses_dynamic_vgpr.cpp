// Tests for wave64-on-rdna4-compute-misses-dynamic-vgpr (Phase 8 v0.10 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_wave64_on_rdna4_compute_misses_dynamic_vgpr();
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
    const auto src = sources.add_buffer("w64rdna4.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_wave64_on_rdna4_compute_misses_dynamic_vgpr());
    Config cfg{};
    cfg.experimental_target_value = target;
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"cs_6_6"};
    return lint(sources, src, rules, cfg, std::filesystem::path{"w64rdna4.hlsl"}, opts);
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

TEST_CASE("wave64-on-rdna4-compute-misses-dynamic-vgpr fires under Rdna4 + WaveSize(64)",
          "[rules][wave64-on-rdna4-compute-misses-dynamic-vgpr]") {
    const std::string hlsl = R"hlsl(
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK(has_rule(lint_under(hlsl, ExperimentalTarget::Rdna4),
                   "wave64-on-rdna4-compute-misses-dynamic-vgpr"));
}

TEST_CASE("wave64-on-rdna4-compute-misses-dynamic-vgpr silent under Rdna4 + WaveSize(32)",
          "[rules][wave64-on-rdna4-compute-misses-dynamic-vgpr]") {
    const std::string hlsl = R"hlsl(
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::Rdna4),
                         "wave64-on-rdna4-compute-misses-dynamic-vgpr"));
}

TEST_CASE("wave64-on-rdna4-compute-misses-dynamic-vgpr silent under default config",
          "[rules][wave64-on-rdna4-compute-misses-dynamic-vgpr][experimental]") {
    const std::string hlsl = R"hlsl(
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::None),
                         "wave64-on-rdna4-compute-misses-dynamic-vgpr"));
}
