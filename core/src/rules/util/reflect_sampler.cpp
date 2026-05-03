// Implementation of the sampler-descriptor helpers declared in
// `reflect_sampler.hpp`.
//
// Today this is a forward-compatible stub. The SlangBridge populated by
// sub-phase 3a does not yet pull sampler-descriptor state out of Slang's
// reflection tree (Slang's HLSL reflection does not expose the inline
// sampler-state syntax in a stable way across the pinned 2026.7.1 surface).
// We return `std::nullopt` for every sampler binding so rules that ask the
// question "what is the MaxAnisotropy of this sampler?" learn "we don't know"
// and skip themselves cleanly. As the bridge surfaces more sampler info, the
// `nullopt` short-circuit below is replaced with a real lookup.

#include "rules/util/reflect_sampler.hpp"

#include <optional>
#include <string_view>

#include "shader_clippy/reflection.hpp"
#include "rules/util/reflect_resource.hpp"

namespace shader_clippy::rules::util {

std::optional<SamplerDescriptor> sampler_descriptor_for(const ReflectionInfo& reflection,
                                                        std::string_view sampler_name) noexcept {
    const auto* binding = reflection.find_binding_by_name(sampler_name);
    if (binding == nullptr) {
        return std::nullopt;
    }
    if (!is_sampler(binding->kind)) {
        return std::nullopt;
    }

    // Forward-compatible stub. The bridge does not yet surface descriptor
    // fields; once it does, populate the SamplerDescriptor here and return it.
    // Rule authors handle `nullopt` as "descriptor unavailable -- skip rule"
    // rather than as an error condition.
    return std::nullopt;
}

}  // namespace shader_clippy::rules::util
