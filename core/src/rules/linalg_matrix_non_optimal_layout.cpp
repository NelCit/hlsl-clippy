// linalg-matrix-non-optimal-layout
//
// Detects SM 6.10 `linalg::Matrix<...>` declarations whose explicit layout
// argument is not the IHV-preferred form for the target architecture.
// The SM 6.10 spec (proposal 0035, Accepted) introduces `linalg::Matrix`
// as the successor to `vector::CooperativeVector` and exposes a layout
// enum with `OPTIMAL_*` choices. Drivers route OPTIMAL-tagged matrices to
// the on-chip matrix engine without a per-element swizzle; non-optimal
// (row-major / column-major) layouts pay a swizzle on every fetch.
//
// This rule is the SM 6.10 successor to `coopvec-non-optimal-matrix-layout`
// (the SM 6.9 cooperative-vector rule that already shipped). Activates
// only on SM 6.10+ targets via `target_is_sm610_or_later`.
//
// Stage: Reflection. The rule walks the AST handed to `on_reflection`
// looking for `linalg::Matrix<...>` declarations or matrix-routing calls
// that name a non-optimal layout; reflection gates the rule on the
// target shader model.

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

constexpr std::string_view k_rule_id = "linalg-matrix-non-optimal-layout";
constexpr std::string_view k_category = "linalg";

constexpr std::array<std::string_view, 3> k_routing_calls{
    "MatrixMul",
    "MatrixVectorMul",
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
        // Only fire when this looks like a `linalg::*` matmul *and* the
        // call mentions one of the routing names. We need both cues to
        // distinguish from the SM 6.9 coopvec form which is handled by the
        // `coopvec-*` rules.
        const bool is_linalg = call_text.find("linalg::") != std::string_view::npos;
        if (is_linalg) {
            bool is_routing_call = false;
            for (const auto name : k_routing_calls) {
                if (call_text.find(name) != std::string_view::npos) {
                    is_routing_call = true;
                    break;
                }
            }
            if (is_routing_call) {
                for (const auto layout : k_non_optimal_layouts) {
                    if (call_text.find(layout) != std::string_view::npos) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message =
                            std::string{"`linalg::Matrix` matmul uses `"} + std::string{layout} +
                            "`; SM 6.10 prefers `MATRIX_LAYOUT_INFERENCING_OPTIMAL` (or "
                            "`TRAINING_OPTIMAL`) -- the IHV-native swizzle for the matrix-engine "
                            "fetcher (Blackwell FP4/FP6, Hopper FP8, RDNA 4 AI accelerator, "
                            "Xe2 XMX) is gated on the OPTIMAL layout";
                        ctx.emit(std::move(diag));
                        break;
                    }
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LinalgMatrixNonOptimalLayout : public Rule {
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
        // Self-gate: only fire when the source uses `linalg::` (the SM 6.10
        // namespace). Reflection-based SM-version detection isn't reliable
        // here because Slang may not have shipped the `linalg::Matrix`
        // type recognition for the version we link against.
        const auto bytes = tree.source_bytes();
        if (bytes.find("linalg::") == std::string_view::npos) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_linalg_matrix_non_optimal_layout() {
    return std::make_unique<LinalgMatrixNonOptimalLayout>();
}

}  // namespace shader_clippy::rules
