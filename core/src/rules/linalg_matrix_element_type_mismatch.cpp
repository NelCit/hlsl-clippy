// linalg-matrix-element-type-mismatch
//
// Detects mixed-precision SM 6.10 `linalg::Matrix` chains where the element
// type of the matrix does not match the connected accumulator's element
// kind, e.g. an `fp16` matrix accumulating into an `fp32` result without
// an explicit conversion node. The matrix-engine fetcher silently widens
// the matrix's elements to the accumulator's precision -- a per-element
// conversion that costs throughput on every IHV's matrix engine.
//
// Stage: Reflection. Activates only on SM 6.10+ targets. Detection is
// AST-driven over the call-text because Slang reflection does not yet
// surface the matrix-engine descriptor pair.

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

constexpr std::string_view k_rule_id = "linalg-matrix-element-type-mismatch";
constexpr std::string_view k_category = "linalg";

constexpr std::array<std::string_view, 3> k_routing_calls{
    "MatrixMul",
    "MatrixVectorMul",
    "OuterProductAccumulate",
};

constexpr std::array<std::string_view, 4> k_low_precision_components{
    "COMPONENT_TYPE_FLOAT16",
    "COMPONENT_TYPE_FLOAT_E4M3",
    "COMPONENT_TYPE_FLOAT_E5M2",
    "COMPONENT_TYPE_BFLOAT16",
};

constexpr std::array<std::string_view, 2> k_high_precision_components{
    "COMPONENT_TYPE_FLOAT32",
    "COMPONENT_TYPE_FLOAT64",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
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
                bool has_low = false;
                bool has_high = false;
                for (const auto t : k_low_precision_components) {
                    if (call_text.find(t) != std::string_view::npos) {
                        has_low = true;
                        break;
                    }
                }
                for (const auto t : k_high_precision_components) {
                    if (call_text.find(t) != std::string_view::npos) {
                        has_high = true;
                        break;
                    }
                }
                if (has_low && has_high) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message =
                        "`linalg::Matrix` chain mixes a low-precision matrix element type "
                        "(fp16 / fp8 / bfloat16) with a high-precision accumulator (fp32 / "
                        "fp64) -- the matrix-engine fetcher silently widens the matrix's "
                        "elements to the accumulator's precision, costing throughput on "
                        "every IHV's matrix engine";
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

class LinalgMatrixElementTypeMismatch : public Rule {
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
        // Self-gate on `linalg::` source marker (SM 6.10 namespace).
        const auto bytes = tree.source_bytes();
        if (bytes.find("linalg::") == std::string_view::npos) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_linalg_matrix_element_type_mismatch() {
    return std::make_unique<LinalgMatrixElementTypeMismatch>();
}

}  // namespace shader_clippy::rules
