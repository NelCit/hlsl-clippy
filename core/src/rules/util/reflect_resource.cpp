// Implementation of the resource-type query helpers declared in
// `reflect_resource.hpp`. Pure value-type accessors -- no Slang dependency,
// no allocation, no exception path.

#include "rules/util/reflect_resource.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

#include "hlsl_clippy/reflection.hpp"

namespace hlsl_clippy::rules::util {

bool is_writable(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::RWBuffer:
        case ResourceKind::RWByteAddressBuffer:
        case ResourceKind::RWStructuredBuffer:
        case ResourceKind::AppendStructuredBuffer:
        case ResourceKind::ConsumeStructuredBuffer:
        case ResourceKind::RWTexture1D:
        case ResourceKind::RWTexture2D:
        case ResourceKind::RWTexture3D:
        case ResourceKind::RWTexture1DArray:
        case ResourceKind::RWTexture2DArray:
            return true;
        default:
            return false;
    }
}

bool is_texture(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::Texture1D:
        case ResourceKind::Texture2D:
        case ResourceKind::Texture3D:
        case ResourceKind::TextureCube:
        case ResourceKind::Texture1DArray:
        case ResourceKind::Texture2DArray:
        case ResourceKind::TextureCubeArray:
        case ResourceKind::RWTexture1D:
        case ResourceKind::RWTexture2D:
        case ResourceKind::RWTexture3D:
        case ResourceKind::RWTexture1DArray:
        case ResourceKind::RWTexture2DArray:
        case ResourceKind::FeedbackTexture2D:
        case ResourceKind::FeedbackTexture2DArray:
            return true;
        default:
            return false;
    }
}

bool is_buffer(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::Buffer:
        case ResourceKind::RWBuffer:
        case ResourceKind::ByteAddressBuffer:
        case ResourceKind::RWByteAddressBuffer:
        case ResourceKind::StructuredBuffer:
        case ResourceKind::RWStructuredBuffer:
        case ResourceKind::AppendStructuredBuffer:
        case ResourceKind::ConsumeStructuredBuffer:
        case ResourceKind::ConstantBuffer:
            return true;
        default:
            return false;
    }
}

bool is_sampler(ResourceKind kind) noexcept {
    return kind == ResourceKind::SamplerState || kind == ResourceKind::SamplerComparisonState;
}

const ResourceBinding* find_binding_used_by(const ReflectionInfo& reflection,
                                            std::string_view name) noexcept {
    return reflection.find_binding_by_name(name);
}

std::optional<std::uint32_t> array_size_of(const ReflectionInfo& reflection,
                                           std::string_view name) noexcept {
    const auto* binding = reflection.find_binding_by_name(name);
    if (binding == nullptr) {
        return std::nullopt;
    }
    return binding->array_size;
}

}  // namespace hlsl_clippy::rules::util
