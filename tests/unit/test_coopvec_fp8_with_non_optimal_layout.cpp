// Tests for the coopvec-fp8-with-non-optimal-layout rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_fp8_with_non_optimal_layout();
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
    rules.push_back(hlsl_clippy::rules::make_coopvec_fp8_with_non_optimal_layout());
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

TEST_CASE("coopvec-fp8-with-non-optimal-layout fires on FP8 with ROW_MAJOR",
          "[rules][coopvec-fp8-with-non-optimal-layout]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, COMPONENT_TYPE_FLOAT_E4M3,
                    weights, 0, 64,
                    COMPONENT_TYPE_FLOAT_E4M3,
                    MATRIX_LAYOUT_ROW_MAJOR);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coopvec-fp8-with-non-optimal-layout"));
}

TEST_CASE("coopvec-fp8-with-non-optimal-layout silent on FP8 with OPTIMAL",
          "[rules][coopvec-fp8-with-non-optimal-layout]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, COMPONENT_TYPE_FLOAT_E4M3,
                    weights, 0, 64,
                    COMPONENT_TYPE_FLOAT_E4M3,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coopvec-fp8-with-non-optimal-layout");
    }
}
