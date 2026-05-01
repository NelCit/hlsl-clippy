// Tests for the coopvec-non-uniform-matrix-handle rule (forward-compatible-
// stub: divergence detected via the canonical SV_DispatchThreadID source).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_non_uniform_matrix_handle();
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
    rules.push_back(hlsl_clippy::rules::make_coopvec_non_uniform_matrix_handle());
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

TEST_CASE("coopvec-non-uniform-matrix-handle fires on SV_DispatchThreadID-derived offset",
          "[rules][coopvec-non-uniform-matrix-handle]") {
    // Phase 3 stub fires on a literal `SV_DispatchThreadID` (or other
    // canonical divergent-source identifier) inside the call's argument
    // list. The Phase 4 uniformity oracle replaces this with a precise
    // taint-propagation check that follows aliases such as `uint tid :
    // SV_DispatchThreadID; ... f(tid)`.
    const std::string hlsl = R"hlsl(
void f(uint3 dtid : SV_DispatchThreadID) {
    MatrixVectorMul(out, in, weights, SV_DispatchThreadID, 64, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "coopvec-non-uniform-matrix-handle"));
}

TEST_CASE("coopvec-non-uniform-matrix-handle silent on uniform args",
          "[rules][coopvec-non-uniform-matrix-handle]") {
    const std::string hlsl = R"hlsl(
void f() {
    MatrixVectorMul(out, in, weights, g_MatOffset, 64, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "coopvec-non-uniform-matrix-handle");
    }
}
