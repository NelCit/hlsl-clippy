// Public reflection types exposed to rule authors.
//
// This header is the only public surface for Slang reflection in the lint
// engine. Per ADR 0012, no Slang handles or `<slang.h>` types are allowed to
// leak across the public API boundary -- every type defined here is a copyable
// / movable value type that the bridge populates by walking Slang's reflection
// tree once per (SourceId, target_profile) tuple.
//
// Rule authors with `stage() == Stage::Reflection` receive a `const
// ReflectionInfo&` in their `on_reflection` hook and use the helpers below to
// answer questions like "is this resource a UAV?" or "what's the byte offset
// of field X in cbuffer Y?". They never see Slang directly.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shader_clippy/source.hpp"

namespace shader_clippy {

/// Coarse-grained classification of a shader resource binding. Mirrors the
/// HLSL resource type system at the granularity rule authors care about. The
/// bridge maps Slang's per-resource shape / type onto this enum; if a Slang
/// resource cannot be mapped (e.g. a vendor-specific extension), it surfaces
/// as `Unknown` and the rule must decide whether to skip or warn.
enum class ResourceKind : std::uint8_t {
    Unknown,
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Texture1DArray,
    Texture2DArray,
    TextureCubeArray,
    RWTexture1D,
    RWTexture2D,
    RWTexture3D,
    RWTexture1DArray,
    RWTexture2DArray,
    Buffer,
    RWBuffer,
    ByteAddressBuffer,
    RWByteAddressBuffer,
    StructuredBuffer,
    RWStructuredBuffer,
    AppendStructuredBuffer,
    ConsumeStructuredBuffer,
    SamplerState,
    SamplerComparisonState,
    ConstantBuffer,
    AccelerationStructure,
    FeedbackTexture2D,
    FeedbackTexture2DArray,
};

/// One resource binding parsed out of Slang's reflection. `register_slot` and
/// `register_space` correspond to HLSL's `register(tN, spaceM)` slot/space
/// pair. `array_size` is `nullopt` when the resource is unbounded
/// (e.g. `Texture2D tex[]`) or when Slang's reflection does not surface an
/// array dimension.
struct ResourceBinding {
    std::string name;
    ResourceKind kind = ResourceKind::Unknown;
    std::uint32_t register_slot = 0;
    std::uint32_t register_space = 0;
    std::optional<std::uint32_t> array_size;
    /// Best-effort AST anchor. Reflection rules that need a precise span use
    /// the `AstTree` they're handed alongside this struct; this field exists
    /// for rules that have no AST anchor available and need to render a
    /// diagnostic anyway.
    Span declaration_span{};

    /// DXGI format string (e.g. `"DXGI_FORMAT_R32G32B32A32_FLOAT"`,
    /// `"DXGI_FORMAT_R8G8B8A8_UNORM"`, `"DXGI_FORMAT_R16G16_SNORM"`).
    /// Populated for typed resources (`Texture2D<float4>`, `Buffer<uint2>`,
    /// `RWTexture3D<unorm float4>`, ...) by mapping the resource's element
    /// type from Slang's reflection. Empty when:
    ///   * the resource is untyped (`ByteAddressBuffer`, `RWByteAddressBuffer`,
    ///     `StructuredBuffer<T>`, `ConstantBuffer<T>`, `SamplerState`,
    ///     `RaytracingAccelerationStructure`);
    ///   * Slang couldn't surface the format (vendor-specific extensions,
    ///     unrecognised template arg);
    ///   * the binding is not a typed-resource at all.
    /// Consumers who need to compare against a known DXGI value should match
    /// the suffix after `DXGI_FORMAT_` (e.g. `"R8G8B8A8_UNORM"`) — the prefix
    /// is intentionally included so the field is paste-into-D3D12-headers
    /// ready.
    std::string dxgi_format;
};

/// One field in a constant-buffer layout. Offsets and sizes are in bytes and
/// reflect Slang's view of the layout (which already accounts for HLSL packing
/// rules). `type_name` is a human-readable rendering used in diagnostics
/// ("float3", "row_major float4x4", etc.).
struct CBufferField {
    std::string name;
    std::uint32_t byte_offset = 0;
    std::uint32_t byte_size = 0;
    std::string type_name;
};

/// One constant-buffer layout. Holds the cbuffer's name, its total byte size,
/// and an in-order list of fields. Helpers compute padding-hole and alignment
/// information used by Phase 3 cbuffer-layout rules.
struct CBufferLayout {
    std::string name;
    std::uint32_t total_bytes = 0;
    std::vector<CBufferField> fields;
    Span declaration_span{};

    /// Total padding between fields, in bytes. Computed as
    /// `total_bytes - sum(field.byte_size)`. Returns `0` if the field list is
    /// dense (no padding) or if reflection reported an empty cbuffer.
    [[nodiscard]] std::uint32_t padding_bytes() const noexcept;

    /// True when `total_bytes` is a multiple of 16. HLSL constant buffers are
    /// laid out in 16-byte rows, so this is a useful pre-condition for many
    /// cbuffer rules.
    [[nodiscard]] bool is_16byte_aligned() const noexcept;
};

/// One entry-point parsed out of Slang's reflection. `stage` is a lowercase
/// stage tag ("vertex", "pixel", "compute", "mesh", "amplification", ...).
/// `numthreads` is populated only for compute / mesh / amplification stages
/// (and only when the entry point declares one). `wave_size_min` /
/// `wave_size_max` reflect `[WaveSize(min, max)]` attributes when present.
struct EntryPointInfo {
    std::string name;
    std::string stage;
    std::optional<std::array<std::uint32_t, 3>> numthreads;
    std::optional<std::uint32_t> wave_size_min;
    std::optional<std::uint32_t> wave_size_max;
    Span declaration_span{};
};

/// Aggregate reflection result for one (SourceId, target_profile) tuple. The
/// `ReflectionEngine` produces exactly one `ReflectionInfo` per cache key,
/// regardless of entry-point count -- the per-entry-point split lives inside
/// the `entry_points` vector.
struct ReflectionInfo {
    std::vector<ResourceBinding> bindings;
    std::vector<CBufferLayout> cbuffers;
    std::vector<EntryPointInfo> entry_points;
    /// Profile string used to compile, e.g. `"sm_6_6"`. The bridge stamps the
    /// profile actually used (which may differ from the user-supplied
    /// `LintOptions::target_profile` when the engine fell back to a per-stage
    /// default).
    std::string target_profile;

    [[nodiscard]] const ResourceBinding* find_binding_by_name(std::string_view name) const noexcept;
    [[nodiscard]] const CBufferLayout* find_cbuffer_by_name(std::string_view name) const noexcept;
    [[nodiscard]] const EntryPointInfo* find_entry_point_by_name(
        std::string_view name) const noexcept;
};

}  // namespace shader_clippy
