// Implementation of the public helpers declared in
// `core/include/hlsl_clippy/reflection.hpp`. Pure value-type accessors -- no
// Slang dependency. Lives under `core/src/reflection/` for code-organisation
// reasons; the bridge / engine in this directory are unrelated to these
// no-op helpers.

#include "hlsl_clippy/reflection.hpp"

#include <cstdint>
#include <string_view>

namespace hlsl_clippy {

std::uint32_t CBufferLayout::padding_bytes() const noexcept {
    std::uint32_t used = 0U;
    for (const auto& f : fields) {
        used += f.byte_size;
    }
    return total_bytes > used ? total_bytes - used : 0U;
}

bool CBufferLayout::is_16byte_aligned() const noexcept {
    return (total_bytes % 16U) == 0U;
}

const ResourceBinding* ReflectionInfo::find_binding_by_name(
    std::string_view binding_name) const noexcept {
    for (const auto& b : bindings) {
        if (b.name == binding_name) {
            return &b;
        }
    }
    return nullptr;
}

const CBufferLayout* ReflectionInfo::find_cbuffer_by_name(
    std::string_view cbuffer_name) const noexcept {
    for (const auto& c : cbuffers) {
        if (c.name == cbuffer_name) {
            return &c;
        }
    }
    return nullptr;
}

const EntryPointInfo* ReflectionInfo::find_entry_point_by_name(
    std::string_view entry_point_name) const noexcept {
    for (const auto& e : entry_points) {
        if (e.name == entry_point_name) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace hlsl_clippy
