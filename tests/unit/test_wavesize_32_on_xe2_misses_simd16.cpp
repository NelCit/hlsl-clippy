// Tests for wavesize-32-on-xe2-misses-simd16 (Phase 8 v0.10 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_wavesize_32_on_xe2_misses_simd16();
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
    const auto src = sources.add_buffer("ws32xe2.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_wavesize_32_on_xe2_misses_simd16());
    Config cfg{};
    cfg.experimental_target_value = target;
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"cs_6_6"};
    return lint(sources, src, rules, cfg, std::filesystem::path{"ws32xe2.hlsl"}, opts);
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

TEST_CASE("wavesize-32-on-xe2-misses-simd16 fires under Xe2 + WaveSize(32)",
          "[rules][wavesize-32-on-xe2-misses-simd16]") {
    const std::string hlsl = R"hlsl(
[WaveSize(32)]
[numthreads(32, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK(has_rule(lint_under(hlsl, ExperimentalTarget::Xe2),
                   "wavesize-32-on-xe2-misses-simd16"));
}

TEST_CASE("wavesize-32-on-xe2-misses-simd16 silent under Xe2 + WaveSize(16)",
          "[rules][wavesize-32-on-xe2-misses-simd16]") {
    const std::string hlsl = R"hlsl(
[WaveSize(16)]
[numthreads(32, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::Xe2),
                         "wavesize-32-on-xe2-misses-simd16"));
}

TEST_CASE("wavesize-32-on-xe2-misses-simd16 silent under default config",
          "[rules][wavesize-32-on-xe2-misses-simd16][experimental]") {
    const std::string hlsl = R"hlsl(
[WaveSize(32)]
[numthreads(32, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::None),
                         "wavesize-32-on-xe2-misses-simd16"));
}
