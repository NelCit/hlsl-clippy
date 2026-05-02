// Tests for the linalg-matrix-non-optimal-layout rule (Phase 8 v0.8 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_linalg_matrix_non_optimal_layout();
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
    const auto src = sources.add_buffer("lmnol.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_linalg_matrix_non_optimal_layout());
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

TEST_CASE("linalg-matrix-non-optimal-layout fires on row-major linalg::Matrix matmul on SM 6.10",
          "[rules][linalg-matrix-non-optimal-layout]") {
    const std::string hlsl = R"hlsl(
#include <linalg.h>

void cs_main() {
    linalg::MatrixMul<linalg::Matrix<float, 4, 4>>(MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                   "linalg-matrix-non-optimal-layout"));
}

TEST_CASE("linalg-matrix-non-optimal-layout silent when layout is INFERENCING_OPTIMAL",
          "[rules][linalg-matrix-non-optimal-layout]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    linalg::MatrixMul<linalg::Matrix<float, 4, 4>>(MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                         "linalg-matrix-non-optimal-layout"));
}

TEST_CASE("linalg-matrix-non-optimal-layout silent on source without linalg::",
          "[rules][linalg-matrix-non-optimal-layout]") {
    // The rule self-gates on the `linalg::` source marker (the SM 6.10
    // namespace), so source without it never fires.
    const std::string hlsl = R"hlsl(
void cs_main() {
    MatrixMul(MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_9"), "linalg-matrix-non-optimal-layout"));
}
