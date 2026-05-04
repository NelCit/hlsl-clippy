// dot-on-axis-aligned-vector
//
// Detects `dot(v, float3(1, 0, 0))` and the other axis-aligned variants for
// `float2`, `float3`, and `float4`, and suggests rewriting as a swizzle:
//
//   dot(v, float3(1, 0, 0))  -->  v.x
//   dot(v, float3(0, 1, 0))  -->  v.y
//   dot(v, float3(0, 0, 1))  -->  v.z
//   dot(v, float4(0, 0, 0, 1))  -->  v.w
//
// A dot product against an axis-aligned constant unit vector is just a
// component pick. The ALU cost of a dot4 is 4 muls + 3 adds; a swizzle is
// free. The constant vector also doesn't need to live in a constant buffer
// or be loaded into a register.
//
// We accept the constant vector on either the left or the right of `dot`.
// We require the constant vector's components to be exactly one `1` and the
// rest exactly `0`, with no leading sign / negation. Any non-axis-aligned
// constant (e.g. `float3(1, 1, 0)`, or `float3(-1, 0, 0)`) is left alone.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/numeric_literal.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;
using util::is_numeric_literal_one;
using util::is_numeric_literal_zero;

constexpr std::string_view k_rule_id = "dot-on-axis-aligned-vector";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_dot_name = "dot";

/// Recognised vector constructor names mapped to their expected component
/// count. We also accept `int2`/`int3`/`int4` since dot is sometimes called
/// on integer vectors.
[[nodiscard]] std::optional<std::uint32_t> vector_ctor_arity(std::string_view name) noexcept {
    if (name == "float2" || name == "int2" || name == "uint2" || name == "half2")
        return 2U;
    if (name == "float3" || name == "int3" || name == "uint3" || name == "half3")
        return 3U;
    if (name == "float4" || name == "int4" || name == "uint4" || name == "half4")
        return 4U;
    return std::nullopt;
}

/// Map an axis index (0=x, 1=y, 2=z, 3=w) to the swizzle character.
[[nodiscard]] char axis_swizzle(std::uint32_t axis) noexcept {
    switch (axis) {
        case 0:
            return 'x';
        case 1:
            return 'y';
        case 2:
            return 'z';
        case 3:
            return 'w';
        default:
            return 'x';
    }
}

/// If `node` is a vector constructor call (e.g. `float3(1, 0, 0)`), inspect
/// its components: if exactly one component is the literal `1` and all
/// others are the literal `0`, return the index (0..3) of the `1` component.
/// Otherwise return `std::nullopt`.
[[nodiscard]] std::optional<std::uint32_t> axis_index_of_constructor(
    ::TSNode node, std::string_view bytes) noexcept {
    if (node_kind(node) != "call_expression")
        return std::nullopt;

    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (::ts_node_is_null(fn))
        return std::nullopt;
    // Accept either an `identifier` or `primitive_type` / `type_identifier`
    // node here -- different versions of tree-sitter-hlsl use different node
    // types for vector type constructors. We use the textual name.
    const auto fn_text = node_text(fn, bytes);
    const auto arity_opt = vector_ctor_arity(fn_text);
    if (!arity_opt)
        return std::nullopt;
    const std::uint32_t arity = *arity_opt;

    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != arity) {
        return std::nullopt;
    }

    std::optional<std::uint32_t> one_index;
    for (std::uint32_t i = 0; i < arity; ++i) {
        const ::TSNode child = ::ts_node_named_child(args, i);
        if (node_kind(child) != "number_literal")
            return std::nullopt;
        const auto t = node_text(child, bytes);
        if (is_numeric_literal_one(t)) {
            if (one_index)
                return std::nullopt;  // More than one `1` -- not axis-aligned.
            one_index = i;
        } else if (!is_numeric_literal_zero(t)) {
            return std::nullopt;
        }
    }
    return one_index;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_dot_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
                const ::TSNode arg0 = ::ts_node_named_child(args, 0);
                const ::TSNode arg1 = ::ts_node_named_child(args, 1);

                std::string_view v_text;
                std::optional<std::uint32_t> axis;

                // Try axis vector on the right (most common: dot(v, float3(1,0,0))).
                if (auto a = axis_index_of_constructor(arg1, bytes); a.has_value()) {
                    v_text = node_text(arg0, bytes);
                    axis = a;
                } else if (auto a2 = axis_index_of_constructor(arg0, bytes); a2.has_value()) {
                    // dot(float3(1,0,0), v) -- commutative.
                    v_text = node_text(arg1, bytes);
                    axis = a2;
                }

                if (axis.has_value() && !v_text.empty()) {
                    const auto call_range = tree.byte_range(node);
                    const char swiz = axis_swizzle(*axis);

                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                    std::string msg =
                        "`dot(v, axis)` against an axis-aligned "
                        "constant unit vector is just a swizzle "
                        "(`v.";
                    msg += swiz;
                    msg +=
                        "`) -- saves a multiply-add chain and avoids loading "
                        "the constant vector";
                    diag.message = std::move(msg);

                    Fix fix;
                    fix.machine_applicable = true;
                    fix.description = std::string{"replace the dot product with a swizzle"};
                    TextEdit edit;
                    edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                    std::string replacement;
                    replacement.reserve(v_text.size() + 2);
                    replacement.append(v_text);
                    replacement.append(".");
                    replacement.push_back(swiz);
                    edit.replacement = std::move(replacement);
                    fix.edits.push_back(std::move(edit));
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

class DotOnAxisAlignedVector : public Rule {
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

std::unique_ptr<Rule> make_dot_on_axis_aligned_vector() {
    return std::make_unique<DotOnAxisAlignedVector>();
}

}  // namespace shader_clippy::rules
