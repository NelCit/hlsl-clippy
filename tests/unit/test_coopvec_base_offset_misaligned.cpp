// Tests for the coopvec-base-offset-misaligned rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_base_offset_misaligned();
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
    rules.push_back(hlsl_clippy::rules::make_coopvec_base_offset_misaligned());
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

TEST_CASE("coopvec-base-offset-misaligned fires on offset 60",
          "[rules][coopvec-base-offset-misaligned]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, /*offset*/ 60, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coopvec-base-offset-misaligned"));
}

TEST_CASE("coopvec-base-offset-misaligned silent on aligned offset 64",
          "[rules][coopvec-base-offset-misaligned]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, /*offset*/ 64, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coopvec-base-offset-misaligned");
    }
}
