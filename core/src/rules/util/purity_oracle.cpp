// Implementation of `purity_oracle.hpp`. See the header for the spec.
//
// The allowlist is the textbook-conservative HLSL pure-intrinsic set: math,
// bit-twiddling, and reinterpret casts. It deliberately excludes:
//   * `Sample` / `Load` / `GetDimensions` (texture / buffer state reads —
//     not strictly side-effectful but the v1.2 use-site treats them as
//     non-duplicable to avoid extra GPU traffic).
//   * `WaveActiveSum` / `WavePrefixSum` / wave-* (cross-lane communication
//     is observable through helper-lane behaviour).
//   * `InterlockedAdd` / `InterlockedExchange` / atomics.
//   * `discard`, `clip`, `abort`, ray-tracing intrinsics.
//
// A future expansion can grow the list without touching callers; the
// oracle is intentionally conservative for v1.2 to keep the conversion-
// sweep agent's diff minimal.

#include "rules/util/purity_oracle.hpp"

#include <array>
#include <cstdint>
#include <string_view>

#include <tree_sitter/api.h>

#include "parser_internal.hpp"
#include "rules/util/ast_helpers.hpp"

namespace hlsl_clippy::rules::util {

namespace {

/// Allowlist of HLSL intrinsics whose call expressions are SideEffectFree
/// when their arguments are SideEffectFree. Sorted alphabetically for
/// readability; lookup is linear because the list is small (~50 entries) and
/// the hot path is one or two probes per `call_expression`.
constexpr std::array<std::string_view, 49> k_pure_intrinsic_allowlist{
    // Numeric reinterpret casts — HLSL's `as*` family.
    "asfloat",
    "asint",
    "asuint",
    // Trigonometric / inverse-trig.
    "acos",
    "asin",
    "atan",
    "atan2",
    "cos",
    "sin",
    "tan",
    // Bit-twiddling.
    "bitfieldextract",
    "bitfieldinsert",
    "countbits",
    "f16tof32",
    "f32tof16",
    "firstbithigh",
    "firstbitlow",
    "reversebits",
    // Vector / linear algebra.
    "cross",
    "dot",
    "length",
    "normalize",
    // Min/max/clamp/select-style — pure value combinators.
    "abs",
    "clamp",
    "max",
    "min",
    "saturate",
    "sign",
    "step",
    // Smooth interpolators.
    "lerp",
    "mad",
    "smoothstep",
    // Rounding / fractional.
    "ceil",
    "floor",
    "frac",
    "round",
    "trunc",
    // Exp / log / pow / sqrt / rsqrt.
    "exp",
    "exp2",
    "log",
    "log10",
    "log2",
    "pow",
    "rsqrt",
    "sqrt",
    // Half-precision bridges.
    "half",
    "min16float",
    "min16int",
    "min16uint",
};

[[nodiscard]] bool is_allowlisted_intrinsic(std::string_view name) noexcept {
    for (const auto entry : k_pure_intrinsic_allowlist) {
        if (entry == name) {
            return true;
        }
    }
    return false;
}

/// Pure-value leaf node kinds: cheap loads with no observable effect.
[[nodiscard]] bool is_pure_leaf(std::string_view kind) noexcept {
    return kind == "identifier" || kind == "field_expression" ||
           kind == "subscript_expression" || kind == "number_literal" ||
           kind == "string_literal";
}

/// Combine two purity values with the "iff every operand is SideEffectFree"
/// rule. Unknown is sticky: any Unknown operand poisons the result. A
/// SideEffectful operand wins over SideEffectFree.
[[nodiscard]] Purity combine(Purity lhs, Purity rhs) noexcept {
    if (lhs == Purity::Unknown || rhs == Purity::Unknown) {
        return Purity::Unknown;
    }
    if (lhs == Purity::SideEffectful || rhs == Purity::SideEffectful) {
        return Purity::SideEffectful;
    }
    return Purity::SideEffectFree;
}

[[nodiscard]] Purity classify_recursive(std::string_view bytes, ::TSNode node) noexcept;

/// Walk every named child and combine their classifications. Used by binary /
/// unary / parenthesized / conditional expressions where "every operand is
/// SideEffectFree" is the recurrence relation.
[[nodiscard]] Purity classify_named_children(std::string_view bytes, ::TSNode node) noexcept {
    const std::uint32_t count = ::ts_node_named_child_count(node);
    if (count == 0U) {
        // No named children — be conservative.
        return Purity::Unknown;
    }
    Purity acc = Purity::SideEffectFree;
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_named_child(node, i);
        if (::ts_node_is_null(child)) {
            return Purity::Unknown;
        }
        acc = combine(acc, classify_recursive(bytes, child));
        if (acc == Purity::Unknown || acc == Purity::SideEffectful) {
            return acc;
        }
    }
    return acc;
}

/// Classify a `call_expression` node. The textual function name is
/// extracted via the `function` field (matches the rest of the rule pack);
/// arguments live under the `arguments` field (a comma-separated list of
/// named children).
[[nodiscard]] Purity classify_call(std::string_view bytes, ::TSNode node) noexcept {
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    const auto fn_text = node_text(fn, bytes);
    if (fn_text.empty() || !is_allowlisted_intrinsic(fn_text)) {
        return Purity::SideEffectful;
    }

    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args)) {
        // Allowlisted intrinsic with no argument node at all (e.g. parser
        // recovery). Treat as Unknown so callers do not duplicate.
        return Purity::Unknown;
    }

    const std::uint32_t named = ::ts_node_named_child_count(args);
    Purity acc = Purity::SideEffectFree;
    for (std::uint32_t i = 0; i < named; ++i) {
        const ::TSNode arg = ::ts_node_named_child(args, i);
        if (::ts_node_is_null(arg)) {
            return Purity::Unknown;
        }
        acc = combine(acc, classify_recursive(bytes, arg));
        if (acc == Purity::Unknown || acc == Purity::SideEffectful) {
            return acc;
        }
    }
    return acc;
}

[[nodiscard]] Purity classify_recursive(std::string_view bytes, ::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return Purity::Unknown;
    }
    const auto kind = node_kind(node);
    if (kind.empty()) {
        return Purity::Unknown;
    }

    if (is_pure_leaf(kind)) {
        return Purity::SideEffectFree;
    }
    if (kind == "binary_expression" || kind == "unary_expression" ||
        kind == "parenthesized_expression" || kind == "conditional_expression") {
        return classify_named_children(bytes, node);
    }
    if (kind == "call_expression") {
        return classify_call(bytes, node);
    }
    if (kind == "assignment_expression" || kind == "update_expression") {
        return Purity::SideEffectful;
    }

    // Anything else (cast_expression, compound_literal, type-specific nodes
    // we haven't enumerated, parse-error nodes) — be conservative.
    return Purity::Unknown;
}

}  // namespace

Purity classify_expression(const AstTree& tree, ::TSNode expr) noexcept {
    return classify_recursive(tree.source_bytes(), expr);
}

}  // namespace hlsl_clippy::rules::util
