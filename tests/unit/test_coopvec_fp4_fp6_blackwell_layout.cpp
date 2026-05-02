// Tests for coopvec-fp4-fp6-blackwell-layout (Phase 8 v0.10 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_fp4_fp6_blackwell_layout();
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
    const auto src = sources.add_buffer("cvfp4.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_coopvec_fp4_fp6_blackwell_layout());
    Config cfg{};
    cfg.experimental_target_value = target;
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_10-preview"};
    return lint(sources, src, rules, cfg, std::filesystem::path{"cvfp4.hlsl"}, opts);
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

TEST_CASE("coopvec-fp4-fp6-blackwell-layout fires under Blackwell + FP4 + non-optimal layout",
          "[rules][coopvec-fp4-fp6-blackwell-layout]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    MatrixVectorMul(COMPONENT_TYPE_FLOAT_E2M1, MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK(has_rule(lint_under(hlsl, ExperimentalTarget::Blackwell),
                   "coopvec-fp4-fp6-blackwell-layout"));
}

TEST_CASE("coopvec-fp4-fp6-blackwell-layout silent under Blackwell + FP4 + optimal layout",
          "[rules][coopvec-fp4-fp6-blackwell-layout]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    MatrixVectorMul(COMPONENT_TYPE_FLOAT_E2M1, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::Blackwell),
                         "coopvec-fp4-fp6-blackwell-layout"));
}

TEST_CASE("coopvec-fp4-fp6-blackwell-layout silent under default config",
          "[rules][coopvec-fp4-fp6-blackwell-layout][experimental]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    MatrixVectorMul(COMPONENT_TYPE_FLOAT_E2M1, MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_under(hlsl, ExperimentalTarget::None),
                         "coopvec-fp4-fp6-blackwell-layout"));
}
