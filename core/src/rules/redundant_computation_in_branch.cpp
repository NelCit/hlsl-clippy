// redundant-computation-in-branch
//
// Detects an `if/else` whose two arms both contain the same statement (modulo
// trailing whitespace), suggesting the statement be hoisted out of the branch.
// Hoisting reduces code size and lets the compiler scalarise the (now branch-
// free) computation.
//
// Stage: Ast. Implementation walks every `if_statement` node, extracts the
// two consequent / alternative blocks, normalises their statement lists, and
// fires when the first statement of each arm is identical text.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "redundant-computation-in-branch";
constexpr std::string_view k_category = "control-flow";

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

/// Collect named child nodes of `block` whose `kind()` is not braces. Most
/// grammars expose statements as named children of the block.
[[nodiscard]] std::vector<::TSNode> block_statements(::TSNode block) {
    std::vector<::TSNode> out;
    if (::ts_node_is_null(block))
        return out;
    const std::uint32_t n = ::ts_node_named_child_count(block);
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        out.push_back(::ts_node_named_child(block, i));
    }
    return out;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "if_statement") {
        const auto consequent = ::ts_node_child_by_field_name(node, "consequence", 11);
        const auto alternative = ::ts_node_child_by_field_name(node, "alternative", 11);
        if (!::ts_node_is_null(consequent) && !::ts_node_is_null(alternative)) {
            // Drill into compound_statement bodies if present.
            ::TSNode then_block = consequent;
            ::TSNode else_block = alternative;
            if (node_kind(then_block) != "compound_statement" &&
                ::ts_node_named_child_count(then_block) > 0U) {
                then_block = ::ts_node_named_child(then_block, 0);
            }
            if (node_kind(else_block) != "compound_statement" &&
                ::ts_node_named_child_count(else_block) > 0U) {
                else_block = ::ts_node_named_child(else_block, 0);
            }
            const auto then_stmts = block_statements(then_block);
            const auto else_stmts = block_statements(else_block);
            if (!then_stmts.empty() && !else_stmts.empty()) {
                const auto first_then = trim(node_text(then_stmts.front(), bytes));
                const auto first_else = trim(node_text(else_stmts.front(), bytes));
                if (!first_then.empty() && first_then == first_else) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{.source = tree.source_id(),
                                             .bytes = tree.byte_range(then_stmts.front())};
                    diag.message = std::string{
                        "first statement of the `then` and `else` arms is "
                        "identical -- hoist it above the `if` to let the "
                        "compiler scalarise the computation"};

                    Fix fix;
                    fix.machine_applicable = false;
                    fix.description = std::string{
                        "hoist the duplicated statement out of the branch; on RDNA/Ada "
                        "the compiler can then schedule the now-uniform op outside the "
                        "divergent region"};
                    diag.fixes.push_back(std::move(fix));

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

class RedundantComputationInBranch : public Rule {
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

std::unique_ptr<Rule> make_redundant_computation_in_branch() {
    return std::make_unique<RedundantComputationInBranch>();
}

}  // namespace hlsl_clippy::rules
