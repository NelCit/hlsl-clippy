// omm-allocaterayquery2-non-const-flags
//
// Detects an `AllocateRayQuery2(constFlags, dynFlags)` call whose first
// argument is not a compile-time constant. DXC requires the first argument
// to be literal -- the entire point of the split is that the compiler
// templates the RayQuery on its bits.
//
// Stage: Ast.
//
// Detection: in a call to `AllocateRayQuery2`, look at the first argument
// expression. The pattern fires when the first argument is an identifier
// that is not a recognised `RAY_FLAG_*` constant -- a global / cbuffer
// variable, for example. Literal flag bundles built from `RAY_FLAG_*`
// expressions stay clean; identifiers from `cbuffer` / function-local sources
// are reported as non-constant.

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

constexpr std::string_view k_rule_id = "omm-allocaterayquery2-non-const-flags";
constexpr std::string_view k_category = "opacity-micromaps";

[[nodiscard]] bool first_arg_looks_constant(std::string_view first_arg) noexcept {
    // Trim whitespace.
    while (!first_arg.empty() && (first_arg.front() == ' ' || first_arg.front() == '\t' ||
                                  first_arg.front() == '\n' || first_arg.front() == '\r')) {
        first_arg.remove_prefix(1U);
    }
    while (!first_arg.empty() && (first_arg.back() == ' ' || first_arg.back() == '\t' ||
                                  first_arg.back() == '\n' || first_arg.back() == '\r')) {
        first_arg.remove_suffix(1U);
    }
    if (first_arg.empty()) {
        return true;  // empty -> bail (nothing to flag)
    }
    // Numeric literal -> constant.
    if (first_arg[0] == '0' || (first_arg[0] >= '1' && first_arg[0] <= '9')) {
        return true;
    }
    // Mention of `RAY_FLAG_` -> assume the OR-bundle is a constant
    // expression of named flag enums.
    if (first_arg.find("RAY_FLAG_") != std::string_view::npos) {
        return true;
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        const auto fn_pos = call_text.find("AllocateRayQuery2");
        if (fn_pos != std::string_view::npos) {
            const auto open = call_text.find('(', fn_pos);
            const auto close = call_text.rfind(')');
            if (open != std::string_view::npos && close != std::string_view::npos && open < close) {
                const auto args = call_text.substr(open + 1U, close - open - 1U);
                // Find the top-level comma separating arg 1 from arg 2,
                // ignoring nested parens.
                int depth = 0;
                std::size_t comma = std::string_view::npos;
                for (std::size_t i = 0; i < args.size(); ++i) {
                    const char c = args[i];
                    if (c == '(') {
                        ++depth;
                    } else if (c == ')') {
                        --depth;
                    } else if (c == ',' && depth == 0) {
                        comma = i;
                        break;
                    }
                }
                const auto first =
                    args.substr(0U, comma == std::string_view::npos ? args.size() : comma);
                if (!first_arg_looks_constant(first)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Error;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "`AllocateRayQuery2` requires the first argument "
                        "(static ray flags) to be a compile-time constant; "
                        "move runtime bits to the second argument"};
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

class OmmAllocateRayQuery2NonConstFlags : public Rule {
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

std::unique_ptr<Rule> make_omm_allocaterayquery2_non_const_flags() {
    return std::make_unique<OmmAllocateRayQuery2NonConstFlags>();
}

}  // namespace hlsl_clippy::rules
