// coopvec-transpose-without-feature-check
//
// Detects a cooperative-vector matmul whose `MATRIX_FLAG_TRANSPOSED` bit is
// set; surfaces a suggestion-grade diagnostic asking the developer to
// confirm an application-side `D3D12_FEATURE_DATA_D3D12_OPTIONS18.
// CooperativeVectorTier` check covers the path. Tier 0 implementations may
// not honour transpose and the runtime fails on dispatch.
//
// Stage: Ast (forward-compatible-stub).
//
// Detection is purely textual on the call expression -- the rule cannot see
// the application-side feature check, so it always fires when the flag is
// set and lets the developer decide. Promotion to a smarter form awaits a
// project-level config that surfaces declared minimum-tier metadata.

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

constexpr std::string_view k_rule_id = "coopvec-transpose-without-feature-check";
constexpr std::string_view k_category = "cooperative-vector";

constexpr std::array<std::string_view, 3> k_calls{
    "MatrixVectorMul",
    "MatrixMul",
    "OuterProductAccumulate",
};

constexpr std::string_view k_transpose_flag = "MATRIX_FLAG_TRANSPOSED";

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
        if (is_target_call && call_text.find(k_transpose_flag) != std::string_view::npos) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "cooperative-vector matmul uses `MATRIX_FLAG_TRANSPOSED` -- "
                "verify an application-side "
                "`CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS18)` covers "
                "the runtime tier; transpose may fail on tier-0 devices"};
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecTransposeWithoutFeatureCheck : public Rule {
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

std::unique_ptr<Rule> make_coopvec_transpose_without_feature_check() {
    return std::make_unique<CoopvecTransposeWithoutFeatureCheck>();
}

}  // namespace shader_clippy::rules
