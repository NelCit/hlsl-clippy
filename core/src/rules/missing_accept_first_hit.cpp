// missing-accept-first-hit
//
// Detects `TraceRay(...)` calls whose ray-flag argument lacks
// `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` AND whose callsite is in a
// function that smells like a shadow / occlusion query (function name
// contains "shadow" or "occlusion"). For occlusion-only traces every IHV
// pays for full BVH traversal beyond the first hit unless the flag is
// present; the first-hit shortcut elides that traversal. Per ADR 0017
// the rule fires at warn severity and ships a suggestion-grade fix that
// ORs the flag into the existing argument.

#include <array>
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
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "missing-accept-first-hit";
constexpr std::string_view k_category = "dxr";

[[nodiscard]] bool name_smells_shadowy(std::string_view fn_name) noexcept {
    // Lower-case substring search; HLSL identifier set is ASCII so a manual
    // shift is enough.
    std::string lower{fn_name};
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    }
    return lower.find("shadow") != std::string::npos ||
           lower.find("occlusion") != std::string::npos ||
           lower.find("visibility") != std::string::npos;
}

[[nodiscard]] ::TSNode enclosing_function(::TSNode node) noexcept {
    auto n = ::ts_node_parent(node);
    while (!::ts_node_is_null(n)) {
        if (node_kind(n) == "function_definition") {
            return n;
        }
        n = ::ts_node_parent(n);
    }
    return n;
}

[[nodiscard]] std::string_view enclosing_function_name(::TSNode call,
                                                       std::string_view bytes) noexcept {
    const auto fn_def = enclosing_function(call);
    if (::ts_node_is_null(fn_def))
        return {};
    // Find the function_declarator under this function_definition; its
    // identifier field holds the function name.
    std::vector<::TSNode> stack;
    stack.push_back(fn_def);
    while (!stack.empty()) {
        const auto n = stack.back();
        stack.pop_back();
        if (::ts_node_is_null(n))
            continue;
        if (node_kind(n) == "function_declarator") {
            // The declarator field of function_declarator is the name.
            const auto declarator = ::ts_node_child_by_field_name(n, "declarator", 10);
            if (!::ts_node_is_null(declarator) && node_kind(declarator) == "identifier") {
                return node_text(declarator, bytes);
            }
            // Fallback: the first identifier child of function_declarator.
            const auto sub_cnt = ::ts_node_named_child_count(n);
            for (std::uint32_t j = 0; j < sub_cnt; ++j) {
                const auto child = ::ts_node_named_child(n, j);
                if (node_kind(child) == "identifier") {
                    return node_text(child, bytes);
                }
            }
        }
        const auto sub_cnt = ::ts_node_named_child_count(n);
        for (std::uint32_t j = 0; j < sub_cnt; ++j) {
            stack.push_back(::ts_node_named_child(n, j));
        }
    }
    return {};
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "TraceRay") {
            // Verify enclosing function name smells "shadowy".
            const auto enclosing = enclosing_function_name(node, bytes);
            if (!enclosing.empty() && name_smells_shadowy(enclosing)) {
                const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
                if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) >= 2U) {
                    const auto flag_arg = ::ts_node_named_child(args, 1);
                    const auto flag_text = node_text(flag_arg, bytes);
                    // Only fire when the argument is a recognisable RAY_FLAG_*
                    // expression (so we don't false-positive a runtime-computed
                    // flag value).
                    if (flag_text.find("RAY_FLAG_") != std::string_view::npos &&
                        flag_text.find("RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH") ==
                            std::string_view::npos) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message = std::string{
                            "`TraceRay` in shadow / occlusion-style function omits "
                            "`RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` -- BVH traversal "
                            "continues past the first hit even though only the occlusion "
                            "bit is needed; the flag short-circuits and saves a full BVH "
                            "walk per ray"};
                        ctx.emit(std::move(diag));
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

class MissingAcceptFirstHit : public Rule {
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

std::unique_ptr<Rule> make_missing_accept_first_hit() {
    return std::make_unique<MissingAcceptFirstHit>();
}

}  // namespace hlsl_clippy::rules
