// coopvec-base-offset-misaligned
//
// Detects a cooperative-vector matmul whose constant `offset` argument is not
// 16-byte aligned (the common min) for non-optimal layouts, or whose `offset`
// is not 64-byte aligned when the layout is OPTIMAL on most IHVs.
//
// Stage: Ast (forward-compatible-stub).
//
// Today the rule scans for the canonical doc-page convention (`/*offset*/ N`)
// and checks the literal integer for alignment. A reflection-driven version
// will compare against the IHV-specific alignment that Slang surfaces once
// the bridge plumbs cooperative-vector facets.

#include <array>
#include <cctype>
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

constexpr std::string_view k_rule_id = "coopvec-base-offset-misaligned";
constexpr std::string_view k_category = "cooperative-vector";

constexpr std::array<std::string_view, 3> k_calls{
    "MatrixVectorMul",
    "MatrixMul",
    "OuterProductAccumulate",
};

constexpr std::uint32_t k_min_alignment = 16U;

[[nodiscard]] bool parse_uint_after_marker(std::string_view text,
                                           std::string_view marker,
                                           std::uint32_t& out) noexcept {
    auto pos = text.find(marker);
    if (pos == std::string_view::npos) {
        return false;
    }
    pos += marker.size();
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    std::uint32_t v = 0;
    bool any = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        v = v * 10U + static_cast<std::uint32_t>(text[pos] - '0');
        any = true;
        ++pos;
    }
    if (!any) {
        return false;
    }
    out = v;
    return true;
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
            std::uint32_t offset = 0;
            if (parse_uint_after_marker(call_text, "/*offset*/", offset)) {
                if ((offset % k_min_alignment) != 0U) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Error;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{"cooperative-vector matmul offset "} +
                                   std::to_string(offset) + " is not 16-byte aligned; " +
                                   "matrix engines on Ada / RDNA 3-4 / Xe-HPG fault or " +
                                   "split the load on misalignment (proposal 0029)";
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

class CoopvecBaseOffsetMisaligned : public Rule {
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

std::unique_ptr<Rule> make_coopvec_base_offset_misaligned() {
    return std::make_unique<CoopvecBaseOffsetMisaligned>();
}

}  // namespace hlsl_clippy::rules
