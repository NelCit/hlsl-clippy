// coopvec-fp8-with-non-optimal-layout
//
// Detects a cooperative-vector matmul that names an FP8 component type
// (`COMPONENT_TYPE_FLOAT_E4M3` / `COMPONENT_TYPE_FLOAT_E5M2`) AND whose
// matrix-layout enum is not one of `MATRIX_LAYOUT_INFERENCING_OPTIMAL` /
// `MATRIX_LAYOUT_TRAINING_OPTIMAL`. The SM 6.9 cooperative-vector spec
// mandates an OPTIMAL layout for FP8 -- the tensor engine's FP8 fetcher
// requires the vendor swizzle.
//
// Stage: Ast (forward-compatible-stub).
//
// Same caveat as `coopvec-non-optimal-matrix-layout`: the bridge does not
// surface cooperative-vector arg metadata, so we operate textually. Catches
// the literal-constant case; sophisticated forms wait for value tracking.

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

constexpr std::string_view k_rule_id = "coopvec-fp8-with-non-optimal-layout";
constexpr std::string_view k_category = "cooperative-vector";

constexpr std::array<std::string_view, 3> k_calls{
    "MatrixVectorMul",
    "MatrixMul",
    "OuterProductAccumulate",
};

constexpr std::array<std::string_view, 2> k_fp8_components{
    "COMPONENT_TYPE_FLOAT_E4M3",
    "COMPONENT_TYPE_FLOAT_E5M2",
};

constexpr std::array<std::string_view, 2> k_optimal_layouts{
    "MATRIX_LAYOUT_INFERENCING_OPTIMAL",
    "MATRIX_LAYOUT_TRAINING_OPTIMAL",
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
            bool has_fp8 = false;
            for (const auto fp8 : k_fp8_components) {
                if (call_text.find(fp8) != std::string_view::npos) {
                    has_fp8 = true;
                    break;
                }
            }
            if (has_fp8) {
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
                    diag.severity = Severity::Error;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "cooperative-vector matmul with FP8 component type "
                        "requires `MATRIX_LAYOUT_INFERENCING_OPTIMAL` or "
                        "`MATRIX_LAYOUT_TRAINING_OPTIMAL`; non-optimal FP8 "
                        "layouts are undefined behaviour (proposal 0029)"};
                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecFp8WithNonOptimalLayout : public Rule {
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

std::unique_ptr<Rule> make_coopvec_fp8_with_non_optimal_layout() {
    return std::make_unique<CoopvecFp8WithNonOptimalLayout>();
}

}  // namespace hlsl_clippy::rules
