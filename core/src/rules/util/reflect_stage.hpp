// Entry-point and target-shader-model helpers for Phase 3 reflection-aware
// rules (ADR 0012 sub-phase 3b).
//
// Scope:
//   - `find_entry_point` -- a thin wrapper around
//     `ReflectionInfo::find_entry_point_by_name` provided for parity with the
//     other utility-namespace helpers.
//   - Stage-classification predicates over `EntryPointInfo::stage`.
//   - Target-profile parsing: `shader_model_minor("sm_6_8")` -> 8;
//     `target_supports_sm("vs_6_7", 7)` -> true.
//   - `wave_size_for_entry_point` -- AST-driven `[WaveSize(N)]` /
//     `[WaveSize(min, max)]` extraction, because Slang's wave-size reflection
//     accessor has churned across recent versions and we get the truth from
//     the source bytes instead.
//
// All helpers are pure-functional value accessors. None of them depend on
// Slang types or the parser internals; the AST-driven helper takes only the
// `AstTree` value type and the `EntryPointInfo` it is querying.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"

namespace hlsl_clippy::rules::util {

/// Look up an `EntryPointInfo` by its declared entry-point name. Returns
/// `nullptr` on miss.
[[nodiscard]] const EntryPointInfo* find_entry_point(const ReflectionInfo& reflection,
                                                     std::string_view entry_name) noexcept;

/// True when `ep.stage == "pixel"`.
[[nodiscard]] bool is_pixel_shader(const EntryPointInfo& ep) noexcept;
/// True when `ep.stage == "vertex"`.
[[nodiscard]] bool is_vertex_shader(const EntryPointInfo& ep) noexcept;
/// True when `ep.stage == "compute"`.
[[nodiscard]] bool is_compute_shader(const EntryPointInfo& ep) noexcept;
/// True when `ep.stage == "mesh"` or `ep.stage == "amplification"`.
[[nodiscard]] bool is_mesh_or_amp_shader(const EntryPointInfo& ep) noexcept;
/// True when `ep.stage` is any DXR raytracing stage:
/// `"raygeneration"` / `"intersection"` / `"anyhit"` / `"closesthit"` /
/// `"miss"` / `"callable"`.
[[nodiscard]] bool is_raytracing_shader(const EntryPointInfo& ep) noexcept;

/// Parse the minor SM version out of a target profile string. Accepts
/// `"sm_6_6"`, `"vs_6_7"`, `"ps_6_8"`, `"cs_6_9"`, etc. Returns the minor
/// version (the digit after the underscore-separated `6`). Returns
/// `std::nullopt` on parse failure or when the major version is not 6.
[[nodiscard]] std::optional<std::uint32_t> shader_model_minor(
    std::string_view target_profile) noexcept;

/// True iff `shader_model_minor(target_profile)` is at least `required_minor`.
/// Returns `false` (not `nullopt`) when the profile cannot be parsed -- rules
/// treat unparseable profiles conservatively as "does not meet requirement".
[[nodiscard]] bool target_supports_sm(std::string_view target_profile,
                                      std::uint32_t required_minor) noexcept;

/// Walk the AST under `tree` looking for a function whose identifier matches
/// `ep.name`, and return the `[WaveSize(N)]` or `[WaveSize(min, max)]`
/// attribute applied to that function as a `(min, max)` pair (single-arg
/// form is reported as `(N, N)`). Returns `std::nullopt` when:
///   - no function matching `ep.name` is found in the tree;
///   - the function has no `[WaveSize]` attribute;
///   - the attribute exists but the argument list cannot be parsed.
///
/// Slang's wave-size reflection accessor has churned across recent versions;
/// reading the attribute out of the source bytes is the stable path until we
/// pin a version that exposes it consistently.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> wave_size_for_entry_point(
    const AstTree& tree, const EntryPointInfo& ep) noexcept;

}  // namespace hlsl_clippy::rules::util
