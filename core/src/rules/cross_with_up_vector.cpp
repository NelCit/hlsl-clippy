// cross-with-up-vector
//
// Detects `cross(v, K)` and `cross(K, v)` where K is an axis-aligned constant
// vector with exactly one non-zero component of value `1` or `-1`. The cross
// product against a cardinal-axis unit vector collapses to a swizzle plus
// (possibly) a sign flip, eliminating six multiplies and three subtracts.
//
// Identities (with K on the right):
//
//   cross(v, float3( 1, 0, 0))  -->  float3( 0,  v.z, -v.y)
//   cross(v, float3(-1, 0, 0))  -->  float3( 0, -v.z,  v.y)
//   cross(v, float3( 0, 1, 0))  -->  float3(-v.z, 0,  v.x)
//   cross(v, float3( 0,-1, 0))  -->  float3( v.z, 0, -v.x)
//   cross(v, float3( 0, 0, 1))  -->  float3( v.y, -v.x, 0)
//   cross(v, float3( 0, 0,-1))  -->  float3(-v.y,  v.x, 0)
//
// `cross(K, v) == -cross(v, K)`; the sign of every component is flipped when
// the constant is on the left.
//
// We only fire when the axis-vector arg is an inline `float3(...)` literal
// (or `int3` / `half3`) whose components are all numeric literals, with one
// component being exactly `1` (or `-1` via a unary minus) and the others
// exactly `0`. Named-constant fold-through is out of scope for this rule.

#include <cstdint>
#include <memory>
#include <optional>
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

constexpr std::string_view k_rule_id = "cross-with-up-vector";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_cross_name = "cross";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

/// True if `text` is a numeric literal whose value is exactly 0.
[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i < text.size() && text[i] >= '1' && text[i] <= '9')
        return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_float_suffix(text[i]))
            return false;
        ++i;
    }
    return true;
}

/// True if `text` is a numeric literal whose value is exactly 1.
[[nodiscard]] bool literal_is_one(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i >= text.size() || text[i] != '1')
        return false;
    ++i;
    if (i < text.size() && text[i] >= '0' && text[i] <= '9')
        return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_float_suffix(text[i]))
            return false;
        ++i;
    }
    return true;
}

/// Component classification: 0, +1, -1, or "anything else".
enum class ComponentKind {
    Zero,
    Positive,
    Negative,
    Other,
};

/// Classify a single argument node within a vector constructor: it may be a
/// bare number_literal (0 or 1), or a unary_expression whose operator is `-`
/// applied to the literal `1` (i.e. `-1`). Everything else is `Other`.
[[nodiscard]] ComponentKind classify_component(::TSNode node, std::string_view bytes) noexcept {
    const auto kind = node_kind(node);
    if (kind == "number_literal") {
        const auto t = node_text(node, bytes);
        if (literal_is_zero(t))
            return ComponentKind::Zero;
        if (literal_is_one(t))
            return ComponentKind::Positive;
        return ComponentKind::Other;
    }
    // tree-sitter-hlsl wraps a leading minus in a unary_expression node.
    if (kind == "unary_expression") {
        // Find the anonymous operator child and the inner operand.
        std::string_view op;
        ::TSNode operand{};
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            ::TSNode child = ::ts_node_child(node, i);
            if (::ts_node_is_null(child))
                continue;
            if (!::ts_node_is_named(child)) {
                if (op.empty())
                    op = node_text(child, bytes);
            } else {
                if (::ts_node_is_null(operand))
                    operand = child;
            }
        }
        // tree-sitter-hlsl may also expose an `argument` field; fall back to it.
        if (::ts_node_is_null(operand)) {
            operand = ::ts_node_child_by_field_name(node, "argument", 8);
        }
        if (op != "-")
            return ComponentKind::Other;
        if (node_kind(operand) != "number_literal")
            return ComponentKind::Other;
        const auto t = node_text(operand, bytes);
        if (literal_is_zero(t))
            return ComponentKind::Zero;
        if (literal_is_one(t))
            return ComponentKind::Negative;
        return ComponentKind::Other;
    }
    return ComponentKind::Other;
}

/// Recognised 3-vector constructor names. Only float3-family is supported; the
/// algebraic identity is the same for int3 / half3.
[[nodiscard]] bool is_float3_ctor(std::string_view name) noexcept {
    return name == "float3" || name == "half3" || name == "int3" || name == "uint3" ||
           name == "min16float3";
}

/// Inspect an argument node: if it is a `float3(c0, c1, c2)` constructor call
/// whose components are each {0, +1, -1} with exactly one of them non-zero,
/// return (axis_index, sign). Otherwise return nullopt.
struct AxisInfo {
    std::uint32_t axis = 0;  // 0=x, 1=y, 2=z
    int sign = 0;            // +1 or -1
};

[[nodiscard]] std::optional<AxisInfo> axis_of_constructor(::TSNode node,
                                                          std::string_view bytes) noexcept {
    if (node_kind(node) != "call_expression")
        return std::nullopt;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (::ts_node_is_null(fn))
        return std::nullopt;
    if (!is_float3_ctor(node_text(fn, bytes)))
        return std::nullopt;

    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 3U) {
        return std::nullopt;
    }

    AxisInfo info{};
    bool found = false;
    for (std::uint32_t i = 0; i < 3; ++i) {
        const ::TSNode c = ::ts_node_named_child(args, i);
        const auto kind = classify_component(c, bytes);
        if (kind == ComponentKind::Zero) {
            continue;
        }
        if (kind == ComponentKind::Positive || kind == ComponentKind::Negative) {
            if (found)
                return std::nullopt;  // >1 non-zero component.
            found = true;
            info.axis = i;
            info.sign = (kind == ComponentKind::Positive) ? +1 : -1;
            continue;
        }
        return std::nullopt;
    }
    if (!found)
        return std::nullopt;
    return info;
}

/// Build the closed-form replacement text for `cross(v, K)` (or `cross(K, v)`
/// when `flip_sign == true`). `axis` selects which of {x, y, z} carries the
/// non-zero K component, and `sign` is its value (+1 or -1).
[[nodiscard]] std::string build_replacement(std::string_view v_text,
                                            std::uint32_t axis,
                                            int sign,
                                            bool flip_sign) noexcept {
    // For K on axis i (sign s): cross(v, e_i * s) =
    //   axis 0 (x): float3(0, +s*v.z, -s*v.y)
    //   axis 1 (y): float3(-s*v.z, 0, +s*v.x)
    //   axis 2 (z): float3(+s*v.y, -s*v.x, 0)
    // cross(K, v) is the negation of cross(v, K).
    const int s = (flip_sign ? -1 : +1) * sign;
    auto signed_swizzle = [&](int component_sign, char sw) -> std::string {
        std::string out;
        if (component_sign < 0)
            out.push_back('-');
        out.append(v_text);
        out.push_back('.');
        out.push_back(sw);
        return out;
    };

    std::string c0;
    std::string c1;
    std::string c2;
    switch (axis) {
        case 0:  // x-axis
            c0 = "0";
            c1 = signed_swizzle(+s, 'z');
            c2 = signed_swizzle(-s, 'y');
            break;
        case 1:  // y-axis
            c0 = signed_swizzle(-s, 'z');
            c1 = "0";
            c2 = signed_swizzle(+s, 'x');
            break;
        case 2:  // z-axis
            c0 = signed_swizzle(+s, 'y');
            c1 = signed_swizzle(-s, 'x');
            c2 = "0";
            break;
        default:
            c0 = "0";
            c1 = "0";
            c2 = "0";
            break;
    }

    std::string out;
    out.reserve(16 + c0.size() + c1.size() + c2.size());
    out.append("float3(");
    out.append(c0);
    out.append(", ");
    out.append(c1);
    out.append(", ");
    out.append(c2);
    out.push_back(')');
    return out;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_cross_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
                const ::TSNode arg0 = ::ts_node_named_child(args, 0);
                const ::TSNode arg1 = ::ts_node_named_child(args, 1);

                std::optional<AxisInfo> axis_right = axis_of_constructor(arg1, bytes);
                std::optional<AxisInfo> axis_left;
                if (!axis_right.has_value()) {
                    axis_left = axis_of_constructor(arg0, bytes);
                }

                const bool right_is_axis = axis_right.has_value();
                if (right_is_axis || axis_left.has_value()) {
                    const std::string_view v_text =
                        right_is_axis ? node_text(arg0, bytes) : node_text(arg1, bytes);
                    const AxisInfo info = right_is_axis ? *axis_right : *axis_left;
                    if (!v_text.empty()) {
                        const auto call_range = tree.byte_range(node);
                        const std::string replacement =
                            build_replacement(v_text,
                                              info.axis,
                                              info.sign,
                                              /*flip_sign=*/!right_is_axis);

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                        diag.message = std::string{
                            "`cross(v, K)` against an axis-aligned constant unit "
                            "vector collapses to a swizzle plus sign flip -- saves "
                            "six multiplies and three adds per call site"};

                        Fix fix;
                        fix.machine_applicable = true;
                        fix.description = std::string{
                            "replace the cross product with the algebraic closed "
                            "form (swizzle + sign)"};
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                        edit.replacement = replacement;
                        fix.edits.push_back(std::move(edit));
                        diag.fixes.push_back(std::move(fix));

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

class CrossWithUpVector : public Rule {
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

std::unique_ptr<Rule> make_cross_with_up_vector() {
    return std::make_unique<CrossWithUpVector>();
}

}  // namespace hlsl_clippy::rules
