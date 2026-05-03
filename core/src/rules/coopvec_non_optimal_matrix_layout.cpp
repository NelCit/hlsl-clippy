// coopvec-non-optimal-matrix-layout
//
// Detects a `dx::linalg::MatrixMul`, `MatrixVectorMul`, or
// `OuterProductAccumulate` call whose matrix-layout enum argument is not
// `MATRIX_LAYOUT_INFERENCING_OPTIMAL` or `MATRIX_LAYOUT_TRAINING_OPTIMAL`.
// The non-optimal layouts (row-major / column-major) work but pay a per-
// element swizzle on every fetch on every IHV's matrix engine.
//
// Stage: Ast (forward-compatible-stub for Reflection-driven layout
// resolution).
//
// The Slang reflection bridge does not yet surface cooperative-vector layout
// descriptors, so this rule operates by scanning call_expression argument
// text. Catches the literal-constant case; the constant-folded variant
// (`static const uint k_layout = MATRIX_LAYOUT_ROW_MAJOR`) waits for
// Phase 4's value-tracking utilities.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "coopvec-non-optimal-matrix-layout";
constexpr std::string_view k_category = "cooperative-vector";

constexpr std::array<std::string_view, 3> k_calls{
    "MatrixVectorMul",
    "MatrixMul",
    "OuterProductAccumulate",
};

constexpr std::array<std::string_view, 2> k_non_optimal_layouts{
    "MATRIX_LAYOUT_ROW_MAJOR",
    "MATRIX_LAYOUT_COLUMN_MAJOR",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        bool is_target_call = false;
        for (const auto name : k_calls) {
            const auto pos = call_text.find(name);
            if (pos != std::string_view::npos && pos < call_text.find('(')) {
                is_target_call = true;
                break;
            }
        }
        if (is_target_call) {
            for (const auto layout : k_non_optimal_layouts) {
                if (call_text.find(layout) != std::string_view::npos) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{"cooperative-vector matmul uses `"} +
                                   std::string{layout} +
                                   "`; prefer `MATRIX_LAYOUT_INFERENCING_OPTIMAL` (or "
                                   "`TRAINING_OPTIMAL`) for IHV-native swizzle and 2-4x "
                                   "throughput on tensor / WMMA / XMX engines";
                    ctx.emit(std::move(diag));
                    break;
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecNonOptimalMatrixLayout : public Rule {
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

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_coopvec_non_optimal_matrix_layout() {
    return std::make_unique<CoopvecNonOptimalMatrixLayout>();
}

}  // namespace shader_clippy::rules
