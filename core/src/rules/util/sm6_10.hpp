// SM 6.10 utility helpers for v0.8+ rules (ADR 0018).
//
// Scope:
//   - `target_is_sm610_or_later` -- string-based target-profile predicate that
//     handles the SM 6.10+ family (including the `-preview` suffix that the
//     DXC SM 6.10 Agility SDK preview emits).
//   - `is_linalg_matrix_type` -- recogniser for the new `linalg::Matrix<...>`
//     type that supersedes `vector::CooperativeVector` in SM 6.10. Tolerant of
//     whitespace inside the template argument list.
//   - `parse_groupshared_limit_attribute` -- AST-driven extraction of the
//     SM 6.10 `[GroupSharedLimit(<bytes>)]` entry-point attribute (proposal
//     0049 Accepted). Mirrors the textual-prefix scan used by
//     `wave_size_for_entry_point` / `loop-attribute-conflict` because
//     tree-sitter-hlsl v0.2.0 still has gaps around the `[attr]` bracket
//     surface.
//   - `expected_wave_size_for_target` -- best-effort smallest portable wave
//     size for a target profile string. Used by `numthreads-not-wave-aligned`
//     and `dispatchmesh-grid-too-small-for-wave`.
//
// All helpers are pure-functional value accessors. None depend on Slang types;
// the AST-driven helper takes only the `AstTree` value type (forward-declared
// in `hlsl_clippy/rule.hpp`) and an entry-point `TSNode`.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <tree_sitter/api.h>

#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"

namespace hlsl_clippy::rules::util {

/// True when `reflection.target_profile` resolves to SM 6.10 or any later
/// 6.x version. Accepts the `-preview` suffix (returns true) so rules
/// targeting the SM 6.10 Agility SDK preview fire under
/// `target_profile = "sm_6_10-preview"`. Returns false for SM 6.9 and below
/// and for unparseable / empty profile strings.
[[nodiscard]] bool target_is_sm610_or_later(const ReflectionInfo& reflection) noexcept;

/// String-based variant of `target_is_sm610_or_later`. Provided as a separate
/// entry so that AST-only rules can answer the question from a target-profile
/// string without constructing a `ReflectionInfo`.
[[nodiscard]] bool target_is_sm610_or_later(std::string_view target_profile) noexcept;

/// True when `type_name` matches the SM 6.10 `linalg::Matrix<...>` type
/// (proposal 0035, Accepted). Tolerant of whitespace inside the template
/// argument list, so both `linalg::Matrix<float, 4, 4>` and
/// `linalg::Matrix < half , 8 , 8 >` match. Returns false for the SM 6.9
/// predecessor `vector::CooperativeVector<...>` and for any non-linalg
/// type spelling.
[[nodiscard]] bool is_linalg_matrix_type(std::string_view type_name) noexcept;

/// Look at the textual prefix preceding the `entry_point` function's
/// identifier and extract the `[GroupSharedLimit(<bytes>)]` attribute when
/// present. Returns the byte limit on success, `std::nullopt` when the
/// attribute is absent or its argument list cannot be parsed.
///
/// `entry_point` should point at the `function_definition` (or
/// `function_declarator`) node for the entry-point in `tree`. The helper
/// walks the tree to locate the function's identifier and scans the bytes
/// preceding it -- the same defensive style used by
/// `wave_size_for_entry_point` because tree-sitter-hlsl v0.2.0 has known
/// gaps around the `[attr]` bracket syntax.
[[nodiscard]] std::optional<std::uint32_t> parse_groupshared_limit_attribute(
    const AstTree& tree, ::TSNode entry_point) noexcept;

/// Best-effort smallest portable wave size for a target profile string.
/// Returns 32 for `sm_6_5` and later (the `[WaveSize]` attribute and the
/// Wave/Quad intrinsic family are standardised at SM 6.5; portable IHV
/// wave widths are 32 or 64), and 64 for older profiles. Empty / unknown
/// profile strings default to 32 (the modern-IHV default; conservative for
/// rules that reason about wave alignment on dispatched grids).
[[nodiscard]] std::uint32_t expected_wave_size_for_target(std::string_view target_profile) noexcept;

}  // namespace hlsl_clippy::rules::util
