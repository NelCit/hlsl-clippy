// coopvec-fp4-fp6-blackwell-layout
//
// Detects `linalg::Matrix` (or legacy `vector::CooperativeVector`) calls
// with FP4 / FP6 element type using a non-Blackwell-optimal layout when
// targeting Blackwell. Blackwell tensor cores are FP4/FP6-native; the
// optimal layout differs from Hopper FP8.
//
// Stage: Reflection. Gated behind `[experimental.target = blackwell]`.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "coopvec-fp4-fp6-blackwell-layout";
constexpr std::string_view k_category = "blackwell";

constexpr std::array<std::string_view, 3> k_routing_calls{
    "MatrixMul",
    "MatrixVectorMul",
    "OuterProductAccumulate",
};

constexpr std::array<std::string_view, 4> k_fp4_fp6_components{
    "COMPONENT_TYPE_FLOAT_E2M1",   // FP4
    "COMPONENT_TYPE_FLOAT_E3M2",   // FP6
    "COMPONENT_TYPE_FLOAT_E2M3",   // FP6 alt
    "COMPONENT_TYPE_FLOAT_FP4",    // alias
};

constexpr std::array<std::string_view, 2> k_optimal_layouts{
    "MATRIX_LAYOUT_INFERENCING_OPTIMAL",
    "MATRIX_LAYOUT_TRAINING_OPTIMAL",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        bool is_target_call = false;
        for (const auto name : k_routing_calls) {
            if (call_text.find(name) != std::string_view::npos) {
                is_target_call = true;
                break;
            }
        }
        if (is_target_call) {
            bool has_fp4_fp6 = false;
            for (const auto t : k_fp4_fp6_components) {
                if (call_text.find(t) != std::string_view::npos) {
                    has_fp4_fp6 = true;
                    break;
                }
            }
            if (has_fp4_fp6) {
                bool has_optimal = false;
                for (const auto opt : k_optimal_layouts) {
                    if (call_text.find(opt) != std::string_view::npos) {
                        has_optimal = true;
                        break;
                    }
                }
                if (!has_optimal) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message =
                        "matrix call with FP4 / FP6 element type lacks "
                        "`MATRIX_LAYOUT_INFERENCING_OPTIMAL` (or `TRAINING_OPTIMAL`) -- "
                        "Blackwell 5th-gen Tensor Cores are FP4/FP6-native and gate the "
                        "swizzle-free fetch path on the OPTIMAL layout";
                    ctx.emit(std::move(diag));
                }
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecFp4Fp6BlackwellLayout : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept override {
        return ExperimentalTarget::Blackwell;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_coopvec_fp4_fp6_blackwell_layout() {
    return std::make_unique<CoopvecFp4Fp6BlackwellLayout>();
}

}  // namespace hlsl_clippy::rules
