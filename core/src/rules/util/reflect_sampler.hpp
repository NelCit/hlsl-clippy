// Sampler-descriptor helpers for Phase 3 reflection-aware rules
// (ADR 0012 sub-phase 3b).
//
// Forward-compatible API. The current `ResourceBinding` struct in
// `shader_clippy/reflection.hpp` does not yet expose sampler descriptor state
// (filter / address mode / MaxLOD / MaxAnisotropy / ComparisonFunc) -- the
// SlangBridge surface that lands in sub-phase 3a does not pull those fields
// out of Slang's reflection tree. This header exists so Phase 3 sampler rules
// (ADR 0011 §Phase 3 Pack B) can be written against a stable interface; the
// bridge can populate the fields incrementally without breaking rules.
//
// Today every accessor returns `std::nullopt`. As the bridge surfaces more
// sampler info, the implementation in `reflect_sampler.cpp` populates the
// matching `SamplerDescriptor` fields and the call sites do not change.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "shader_clippy/reflection.hpp"

namespace shader_clippy::rules::util {

/// Best-effort sampler descriptor. Each field is `std::optional<...>` because
/// the SlangBridge surfaces these incrementally; rules MUST handle the
/// `nullopt` case (e.g. by skipping the rule when the field they need is not
/// available) rather than asserting on presence. Field names mirror the D3D12
/// sampler-state vocabulary because that is the lingua franca for the rule
/// docs.
struct SamplerDescriptor {
    /// Filter mode, e.g. `"MIN_MAG_MIP_LINEAR"`, `"COMPARISON_MIN_MAG_LINEAR_MIP_POINT"`.
    /// Rendered as a string so rule diagnostics can quote it directly.
    std::optional<std::string> filter;
    /// MaxAnisotropy clamp from the sampler descriptor (range 1..16 in D3D).
    std::optional<std::uint32_t> max_anisotropy;
    /// MaxLOD clamp; `FLT_MAX` is the canonical "no clamp" value in HLSL.
    std::optional<float> max_lod;
    /// MinLOD clamp.
    std::optional<float> min_lod;
    /// Comparison function for `SamplerComparisonState`, e.g. `"LESS_EQUAL"`.
    std::optional<std::string> comparison_func;
    /// Address mode along U, e.g. `"WRAP"`, `"CLAMP"`, `"MIRROR"`.
    std::optional<std::string> address_u;
    /// Address mode along V.
    std::optional<std::string> address_v;
    /// Address mode along W.
    std::optional<std::string> address_w;
};

/// Look up the sampler descriptor for the named sampler binding. Returns
/// `std::nullopt` when:
///   - the named binding does not exist in the reflection result;
///   - the named binding exists but is not a sampler;
///   - the binding is a sampler but the bridge has not yet surfaced its
///     descriptor state (this is the common case today).
///
/// The contract is "if a `SamplerDescriptor` is returned, every populated
/// field is authoritative; absent fields mean the bridge could not surface
/// them". As the SlangBridge grows, more fields become populated without any
/// rule code changing.
[[nodiscard]] std::optional<SamplerDescriptor> sampler_descriptor_for(
    const ReflectionInfo& reflection, std::string_view sampler_name) noexcept;

}  // namespace shader_clippy::rules::util
