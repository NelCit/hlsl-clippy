// Resource-type query helpers for Phase 3 reflection-aware rules
// (ADR 0012 sub-phase 3b).
//
// These helpers sit on top of the value-type `ReflectionInfo` declared in
// `shader_clippy/reflection.hpp`. They classify `ResourceKind` values and
// look up bindings by shader-side identifier name. Rules with
// `stage() == Stage::Reflection` include this header from
// `core/src/rules/util/`; the public surface stays free of Slang types
// because every accessor here operates on the already-materialised
// reflection structs.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "shader_clippy/reflection.hpp"

namespace shader_clippy::rules::util {

/// True when `kind` denotes a writable (UAV-like) resource. Covers `RWBuffer`,
/// every `RWTexture*` variant, `RWByteAddressBuffer`, and `RWStructuredBuffer`.
/// `AppendStructuredBuffer` and `ConsumeStructuredBuffer` are also considered
/// writable because they map to UAVs at the binding level even though their
/// usage discipline is queue-shaped rather than free-form.
[[nodiscard]] bool is_writable(ResourceKind kind) noexcept;

/// True when `kind` denotes any texture resource: read-only `Texture*`
/// variants, their array variants, and the writable `RWTexture*` variants.
/// `FeedbackTexture2D` / `FeedbackTexture2DArray` count as textures here
/// because rules that want "is this a texture binding" expect them to
/// participate; rules that need to discriminate sampler-feedback resources
/// can re-check the kind directly.
[[nodiscard]] bool is_texture(ResourceKind kind) noexcept;

/// True when `kind` denotes any buffer resource: typed (`Buffer` / `RWBuffer`),
/// raw (`ByteAddressBuffer` / `RWByteAddressBuffer`), structured
/// (`StructuredBuffer` / `RWStructuredBuffer` / `AppendStructuredBuffer` /
/// `ConsumeStructuredBuffer`), or constant (`ConstantBuffer`).
[[nodiscard]] bool is_buffer(ResourceKind kind) noexcept;

/// True when `kind` denotes a sampler: `SamplerState` or
/// `SamplerComparisonState`.
[[nodiscard]] bool is_sampler(ResourceKind kind) noexcept;

/// Look up a `ResourceBinding` by its shader-side identifier name. Returns
/// `nullptr` on miss. This is a thin wrapper around
/// `ReflectionInfo::find_binding_by_name` provided for parity with the
/// other utility-namespace helpers and to give rules a single
/// `util::find_binding_used_by(...)` spelling to call.
[[nodiscard]] const ResourceBinding* find_binding_used_by(const ReflectionInfo& reflection,
                                                          std::string_view name) noexcept;

/// Convenience accessor that returns the named binding's `array_size` if
/// present. `nullopt` indicates either the binding does not exist or the
/// resource is unbounded (`Texture2D tex[]`). Rules that need to distinguish
/// "not found" from "unbounded" must call `find_binding_used_by` directly.
[[nodiscard]] std::optional<std::uint32_t> array_size_of(const ReflectionInfo& reflection,
                                                         std::string_view name) noexcept;

}  // namespace shader_clippy::rules::util
