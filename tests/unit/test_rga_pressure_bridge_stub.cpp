// Tests for rga-pressure-bridge-stub (Phase 8 deferred; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_rga_pressure_bridge_stub();
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
    const auto src = sources.add_buffer("rga.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_rga_pressure_bridge_stub());
    Config cfg{};
    cfg.experimental_target_value = target;
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"cs_6_6"};
    return lint(sources, src, rules, cfg, std::filesystem::path{"rga.hlsl"}, opts);
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

TEST_CASE("rga-pressure-bridge-stub fires under Rdna4 once per source",
          "[rules][rga-pressure-bridge-stub]") {
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK(has_rule(lint_under(hlsl, ExperimentalTarget::Rdna4),
                   "rga-pressure-bridge-stub"));
}

TEST_CASE("rga-pressure-bridge-stub silent under default config",
          "[rules][rga-pressure-bridge-stub][experimental]") {
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::None),
                         "rga-pressure-bridge-stub"));
}
