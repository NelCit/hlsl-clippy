// Tests for the coopvec-transpose-without-feature-check rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_transpose_without_feature_check();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_coopvec_transpose_without_feature_check());
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

TEST_CASE("coopvec-transpose-without-feature-check fires on MATRIX_FLAG_TRANSPOSED",
          "[rules][coopvec-transpose-without-feature-check]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, 0, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL,
                    MATRIX_FLAG_TRANSPOSED);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coopvec-transpose-without-feature-check"));
}

TEST_CASE("coopvec-transpose-without-feature-check silent without transpose",
          "[rules][coopvec-transpose-without-feature-check]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, 0, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coopvec-transpose-without-feature-check");
    }
}
