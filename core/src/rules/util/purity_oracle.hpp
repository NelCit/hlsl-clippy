// Side-effect-purity oracle (v1.2 — ADR 0019 §"v1.x patch trajectory").
//
// Classifies a tree-sitter expression node as `SideEffectFree`,
// `SideEffectful`, or `Unknown`. The textbook-conservative rules:
//
//   * Identifier / field access / subscript / number literal / string literal
//     are SideEffectFree (pure value loads — HLSL doesn't model
//     volatile-style observable reads).
//   * Binary / unary / parenthesized / conditional expressions recurse on
//     each operand and combine: SideEffectFree iff every operand is
//     SideEffectFree.
//   * Call expressions are SideEffectful by default. A small allowlist of
//     known-pure HLSL intrinsics (textbook math + bit-twiddle + reinterpret
//     casts) returns SideEffectFree IFF every argument is SideEffectFree.
//   * Assignment / update / unrecognised nodes are SideEffectful or Unknown.
//
// The oracle is conservative on purpose: rules that hoist or duplicate an
// expression must be safe even on torn input, so anything we cannot classify
// returns `Unknown` and callers treat that as "do not transform".
//
// First consumer: the v1.2 conversion-sweep agent that needs to fold
// `clamp(x, 0, 1)` -> `saturate(x)` only when `x` is duplicate-safe. Future
// consumers: any rule that wants to lift a sub-expression out of a loop or
// merge two sample sites.

#pragma once

#include <cstdint>

#include <tree_sitter/api.h>

namespace hlsl_clippy {
class AstTree;
}  // namespace hlsl_clippy

namespace hlsl_clippy::rules::util {

/// Coarse-grained purity classification. The enum is `std::uint8_t` so it
/// fits in a register and ADR 0004's enum-naming rule (CamelCase) holds.
enum class Purity : std::uint8_t {
    /// Analysis didn't reach the node (null node, byte range out of bounds,
    /// or a node kind we don't recognise). Callers should treat this as
    /// "do not transform" — same as `SideEffectful`.
    Unknown,
    /// Expression has no observable effect; it is safe to duplicate, hoist,
    /// or eliminate. Pure value loads, arithmetic, and allowlisted intrinsic
    /// calls fall here.
    SideEffectFree,
    /// Expression contains a call to a non-allowlisted function, an
    /// assignment, an increment/decrement, or any other node that mutates
    /// observable state.
    SideEffectful,
};

/// Classify `expr` against the rules above. The `tree` is needed to slice
/// the source bytes covered by call-target identifiers (the allowlist
/// matches by textual function name); callers must pass the same `AstTree`
/// the node was parsed from. The function is `noexcept` and never allocates
/// — it walks the subtree iteratively where possible.
[[nodiscard]] Purity classify_expression(const AstTree& tree, ::TSNode expr) noexcept;

}  // namespace hlsl_clippy::rules::util
