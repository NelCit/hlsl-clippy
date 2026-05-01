// Tests for the coopvec-non-optimal-matrix-layout rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_non_optimal_matrix_layout();
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
    rules.push_back(hlsl_clippy::rules::make_coopvec_non_optimal_matrix_layout());
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

TEST_CASE("coopvec-non-optimal-matrix-layout fires on ROW_MAJOR matmul",
          "[rules][coopvec-non-optimal-matrix-layout]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, 0, 64, MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coopvec-non-optimal-matrix-layout"));
}

TEST_CASE("coopvec-non-optimal-matrix-layout is silent on OPTIMAL layout",
          "[rules][coopvec-non-optimal-matrix-layout]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, 0, 64, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coopvec-non-optimal-matrix-layout");
    }
}
