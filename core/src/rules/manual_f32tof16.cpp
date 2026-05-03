// manual-f32tof16
//
// Detects bit-twiddling patterns that compile to a software f32tof16 lowering:
// the canonical form `(asuint(x) >> 13) & 0x7FFF | ...`. The HLSL intrinsic
// `f32tof16(x)` maps to a single `cvt` instruction on every IHV (RDNA 1+,
// Turing+, Xe-HPG); rolling the conversion by hand prevents the pattern
// match and burns several extra ALU ops per fp32 value.
//
// Stage: Ast. The detection is textual: locate `asuint` followed within the
// same expression by `>> 13` AND `0x7FFF` masking. That triple is the
// literal signature of a hand-rolled fp32 -> fp16 conversion.

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

constexpr std::string_view k_rule_id = "manual-f32tof16";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool looks_like_manual_f32tof16(std::string_view text) noexcept {
    return text.find("asuint") != std::string_view::npos &&
           text.find(">> 13") != std::string_view::npos &&
           (text.find("0x7FFF") != std::string_view::npos ||
            text.find("0x7fff") != std::string_view::npos);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;
    const auto kind = node_kind(node);
    // Look at function-scope statements; we want the smallest enclosing
    // expression that exhibits the pattern, not the whole TU.
    if (kind == "binary_expression" || kind == "assignment_expression" ||
        kind == "init_declarator" || kind == "return_statement") {
        const auto text = node_text(node, bytes);
        if (looks_like_manual_f32tof16(text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "expression matches the canonical `(asuint(x) >> 13) & 0x7FFF | ...` "
                "lowering of a software fp32 -> fp16 conversion -- replace with the "
                "`f32tof16(x)` intrinsic, which compiles to a single `cvt` instruction"};
            ctx.emit(std::move(diag));
            return;  // do not double-fire on enclosing nodes
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualF32ToF16 : public Rule {
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

std::unique_ptr<Rule> make_manual_f32tof16() {
    return std::make_unique<ManualF32ToF16>();
}

}  // namespace shader_clippy::rules
