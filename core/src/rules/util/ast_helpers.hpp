// Shared tree-sitter AST helpers used across the rule pack.
//
// Before this header existed, every rule TU declared its own anonymous-namespace
// copy of `node_kind`, `node_text`, and `is_id_char`. With ~95 rule TUs, those
// duplicates dominated the post-PCH compile time and any future tweak (e.g. a
// new safety check on `byte_range > size`) had to be applied in every TU.
//
// The helpers here are deliberately conservative on out-of-range / null-node
// inputs: tree-sitter exposes nodes that may have been built from torn input,
// and rules treat an empty result as "no signal — do not fire". Returning an
// empty `std::string_view` instead of crashing keeps the rule pack robust on
// the corpus's deliberately malformed fixtures.
//
// All helpers live in `shader_clippy::rules::util` so call sites read as
// `util::node_kind(...)`, matching the existing `util::reflect_stage` /
// `util::cfg_query` conventions from sub-phases 3b / 4b.

#pragma once

#include <string_view>

#include <tree_sitter/api.h>

namespace shader_clippy::rules::util {

/// Symbolic node type as a `string_view`, e.g. `"call_expression"`. Returns an
/// empty view for null nodes or when tree-sitter reports a null type pointer.
[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept;

/// UTF-8 text covered by `node`, viewed into the `bytes` buffer. Returns an
/// empty view when the node is null or its byte range falls outside `bytes`.
[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept;

/// True when `c` can appear inside an HLSL identifier (`[A-Za-z0-9_]`). Used
/// by rules that scan source bytes for word-boundary identifier matches
/// without re-parsing through tree-sitter.
[[nodiscard]] constexpr bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace shader_clippy::rules::util
