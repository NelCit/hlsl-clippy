// dot4add-opportunity
//
// Detects 4-element packed dot-product expansions of the form
// `a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w + acc` (or variants without
// the trailing accumulator). The sequence is a single SM 6.4 packed
// `dot4add_u8packed` / `dot4add_i8packed` (or the float-vec equivalent
// `dot(a, b)`) on every IHV.
//
// Stage: Ast. Look for `binary_expression` trees of `+` of multiplied
// vector-component pairs. We use a tolerant pattern-matcher: any chain
// of 4 product terms `a.X * b.X` (where X spans all of x/y/z/w in
// either order or rgba) collapsed by `+` qualifies as a dot4 candidate.

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

constexpr std::string_view k_rule_id = "dot4add-opportunity";
constexpr std::string_view k_category = "math";

/// True when `ch` is one of the four canonical vector swizzle channels
/// in either xyzw or rgba flavour.
[[nodiscard]] bool is_swizzle_channel(char ch) noexcept {
    return ch == 'x' || ch == 'y' || ch == 'z' || ch == 'w' || ch == 'r' || ch == 'g' ||
           ch == 'b' || ch == 'a';
}

/// Extract `(base_text, channel)` from a `field_expression` whose field is
/// a single-character swizzle channel. Returns false if the field is not a
/// valid single-channel swizzle.
[[nodiscard]] bool decompose_swizzle(::TSNode node,
                                     std::string_view bytes,
                                     std::string_view& base,
                                     char& channel) noexcept {
    if (node_kind(node) != "field_expression") {
        return false;
    }
    const auto field = ::ts_node_child_by_field_name(node, "field", 5);
    const auto field_text = node_text(field, bytes);
    if (field_text.size() != 1U || !is_swizzle_channel(field_text[0])) {
        return false;
    }
    const auto arg = ::ts_node_child_by_field_name(node, "argument", 8);
    base = node_text(arg, bytes);
    if (base.empty()) {
        return false;
    }
    channel = field_text[0];
    return true;
}

/// True when `node` is `<base_a>.<ch> * <base_b>.<ch>` for some base / ch.
/// Sets out-params on success.
[[nodiscard]] bool is_swizzle_product(::TSNode node,
                                      std::string_view bytes,
                                      std::string_view& base_a,
                                      std::string_view& base_b,
                                      char& channel) noexcept {
    if (node_kind(node) != "binary_expression") {
        return false;
    }
    const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
    const auto op_text = node_text(op, bytes);
    if (op_text != "*") {
        return false;
    }
    const auto lhs = ::ts_node_child_by_field_name(node, "left", 4);
    const auto rhs = ::ts_node_child_by_field_name(node, "right", 5);
    char ch_l = 0;
    char ch_r = 0;
    if (!decompose_swizzle(lhs, bytes, base_a, ch_l)) {
        return false;
    }
    if (!decompose_swizzle(rhs, bytes, base_b, ch_r)) {
        return false;
    }
    if (ch_l != ch_r) {
        return false;
    }
    channel = ch_l;
    return true;
}

/// Flatten a left-leaning `+` tree into a list of operand nodes. Stops at
/// any non-`+`-binary node. Returns true on success.
void flatten_addition(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) noexcept {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "binary_expression") {
        const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
        const auto op_text = node_text(op, bytes);
        if (op_text == "+") {
            const auto lhs = ::ts_node_child_by_field_name(node, "left", 4);
            const auto rhs = ::ts_node_child_by_field_name(node, "right", 5);
            flatten_addition(lhs, bytes, out);
            flatten_addition(rhs, bytes, out);
            return;
        }
    }
    if (node_kind(node) == "parenthesized_expression" && ::ts_node_named_child_count(node) == 1U) {
        flatten_addition(::ts_node_named_child(node, 0), bytes, out);
        return;
    }
    out.push_back(node);
}

[[nodiscard]] bool is_xyzw_or_rgba_set(const std::array<bool, 8>& seen) noexcept {
    // xyzw indices 0..3, rgba indices 4..7. Either all of xyzw or all of rgba.
    const bool all_xyzw = seen[0] && seen[1] && seen[2] && seen[3];
    const bool all_rgba = seen[4] && seen[5] && seen[6] && seen[7];
    return all_xyzw || all_rgba;
}

[[nodiscard]] std::size_t channel_index(char ch) noexcept {
    switch (ch) {
        case 'x':
            return 0U;
        case 'y':
            return 1U;
        case 'z':
            return 2U;
        case 'w':
            return 3U;
        case 'r':
            return 4U;
        case 'g':
            return 5U;
        case 'b':
            return 6U;
        case 'a':
            return 7U;
        default:
            return 8U;
    }
}

void check_addition(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (node_kind(node) != "binary_expression") {
        return;
    }
    const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
    const auto op_text = node_text(op, bytes);
    if (op_text != "+") {
        return;
    }
    std::vector<::TSNode> terms;
    flatten_addition(node, bytes, terms);
    if (terms.size() < 4U) {
        return;
    }

    std::array<bool, 8> seen{};
    seen.fill(false);
    std::string_view first_a;
    std::string_view first_b;
    int product_count = 0;
    for (const auto t : terms) {
        std::string_view a;
        std::string_view b;
        char ch = 0;
        if (!is_swizzle_product(t, bytes, a, b, ch)) {
            continue;
        }
        if (product_count == 0) {
            first_a = a;
            first_b = b;
        } else {
            if (a != first_a || b != first_b) {
                // Mismatched bases -- not a single dot4 candidate.
                return;
            }
        }
        const auto idx = channel_index(ch);
        if (idx >= seen.size()) {
            return;
        }
        seen[idx] = true;
        ++product_count;
    }
    if (product_count == 4 && is_xyzw_or_rgba_set(seen)) {
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
        diag.message = std::string{"4-element dot product expanded as `"} + std::string{first_a} +
                       ".x*" + std::string{first_b} +
                       ".x + ...`; SM 6.4 `dot4add_u8packed` / `dot4add_i8packed` (or `dot(a, b)` "
                       "for floats) compresses the 19-instruction unrolled form into 1 instruction "
                       "on RDNA 2+ / Turing+ / Xe-HPG";
        ctx.emit(std::move(diag));
    }
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "binary_expression") {
        // Only check the topmost `+` chain in any subtree -- we want one
        // diagnostic per 4-product chain, not 4. We approximate "topmost"
        // by checking whether the parent is also a `+`.
        const auto parent = ::ts_node_parent(node);
        bool is_topmost_add = true;
        if (!::ts_node_is_null(parent) && node_kind(parent) == "binary_expression") {
            const auto pop = ::ts_node_child_by_field_name(parent, "operator", 8);
            if (node_text(pop, bytes) == "+") {
                is_topmost_add = false;
            }
        }
        if (is_topmost_add) {
            check_addition(node, bytes, tree, ctx);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class Dot4AddOpportunity : public Rule {
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

std::unique_ptr<Rule> make_dot4add_opportunity() {
    return std::make_unique<Dot4AddOpportunity>();
}

}  // namespace hlsl_clippy::rules
