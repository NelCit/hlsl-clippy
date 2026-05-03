// Tests for linalg-matrix-element-type-mismatch (Phase 8 v0.8 pack; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_linalg_matrix_element_type_mismatch();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  const std::string& profile) {
    SourceManager sources;
    const auto src = sources.add_buffer("lmetm.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_linalg_matrix_element_type_mismatch());
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

TEST_CASE("linalg-matrix-element-type-mismatch fires on fp16 -> fp32 chain",
          "[rules][linalg-matrix-element-type-mismatch]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT32, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                   "linalg-matrix-element-type-mismatch"));
}

TEST_CASE("linalg-matrix-element-type-mismatch silent when both sides are fp16",
          "[rules][linalg-matrix-element-type-mismatch]") {
    const std::string hlsl = R"hlsl(
void cs_main() {
    linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT16, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_10-preview"),
                         "linalg-matrix-element-type-mismatch"));
}
