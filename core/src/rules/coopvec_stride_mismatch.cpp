// coopvec-stride-mismatch
//
// Detects a cooperative-vector matmul whose constant `stride` argument does
// not match the row-stride implied by the matrix shape and component type.
//
// Stage: Ast (forward-compatible-stub).
//
// Properly checking the stride needs:
//   - reflection on the called template / overload to know the matrix shape;
//   - constant-folding on the `stride` integer literal;
//   - the layout enum value (rule does not fire on OPTIMAL layouts).
//
// The Slang reflection bridge does not yet surface coopvec call shapes. The
// Phase 3 stub emits a structured "verify" diagnostic when a coopvec call
// uses a non-optimal layout AND a literal numeric stride argument; it cannot
// authoritatively confirm a mismatch, but it surfaces the call sites that
// the developer should re-check. Promoted to a hard match in Phase 4 once
// reflection grows the necessary facets.

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

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "coopvec-stride-mismatch";
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

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

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
            bool non_optimal = false;
            for (const auto layout : k_non_optimal_layouts) {
                if (call_text.find(layout) != std::string_view::npos) {
                    non_optimal = true;
                    break;
                }
            }
            // Look for `/*stride*/ N` annotation -- the canonical doc-page
            // pattern. Real check awaits reflection.
            const auto stride_marker = call_text.find("/*stride*/");
            if (non_optimal && stride_marker != std::string_view::npos) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "cooperative-vector matmul stride / shape consistency "
                    "cannot be verified from the AST alone -- reflect against "
                    "the matrix shape and component type to confirm "
                    "`stride == rows * sizeof(component)` (proposal 0029)"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecStrideMismatch : public Rule {
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

std::unique_ptr<Rule> make_coopvec_stride_mismatch() {
    return std::make_unique<CoopvecStrideMismatch>();
}

}  // namespace hlsl_clippy::rules
