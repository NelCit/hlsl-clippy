// slang_bridge.cpp -- THE ONE TU under core/ that includes <slang.h>.
//
// Per ADR 0012 (Phase 3 reflection infrastructure):
//   * The bridge owns a process-singleton `IGlobalSession` (lazy-initialised
//     on first use, never mutated after construction). `IGlobalSession` is
//     not thread-safe, so we treat it as read-only post-construction.
//   * The bridge owns a per-instance pool of `ISession` workers, sized at
//     construction time. The pool guards mutation behind a mutex and hands
//     out one `ISession` per `reflect()` call; sessions are returned to the
//     pool on success or failure.
//   * Compile + reflection failures surface as `Diagnostic` with
//     `code = "clippy::reflection"` and `severity = Severity::Error`.
//   * No Slang type ever escapes this TU. Everything the caller sees is a
//     value type defined in `shader_clippy/reflection.hpp`.

#include "reflection/slang_bridge.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <slang-com-ptr.h>
#include <slang.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::reflection {

namespace {

/// Convert a Slang IBlob into an owned std::string. Empty blob -> empty string.
[[nodiscard]] std::string blob_to_string(slang::IBlob* blob) {
    if (blob == nullptr) {
        return {};
    }
    const char* data = static_cast<const char*>(blob->getBufferPointer());
    const std::size_t size = blob->getBufferSize();
    if (data == nullptr || size == 0U) {
        return {};
    }
    return std::string{data, size};
}

/// True when the Slang error blob is *only* complaining about DXC's minimum-
/// precision type family (`min16float`, `min16uint`, `min16int`). Slang's
/// HLSL frontend doesn't recognise these — they're DXC-specific shader-model
/// 6.2+ types that the spec deprecated in favour of `float16_t`/`uint16_t`/
/// `int16_t` — but several rules in this repo (e.g. `min16float-in-cbuffer-
/// roundtrip`, `groupshared-16bit-unpacked`) need to fire on them, and the
/// fixtures that exercise those rules naturally fail Slang reflection.
///
/// We don't want those fixtures (or any user shader using min-precision
/// types intentionally) to scream as red Errors in the IDE. AST-only rules
/// still ran on the source; the only consequence is that reflection-aware
/// rules are skipped for this file.
[[nodiscard]] bool is_min_precision_only_failure(std::string_view blob) noexcept {
    // Cheap heuristic: look for at least one `'min16<X>'` mention in
    // an undefined-identifier diagnostic, AND no other E-prefixed errors
    // pointing at non-min16 identifiers. Slang reports each unknown
    // identifier on its own E30015 line, so a file using `min16uint`
    // twice produces two E30015 lines but no other errors.
    if (blob.find("undefined identifier") == std::string_view::npos) {
        return false;
    }
    bool saw_min16 = false;
    bool saw_non_min16_undef = false;
    std::size_t pos = 0U;
    while (pos < blob.size()) {
        const auto next = blob.find("undefined identifier", pos);
        if (next == std::string_view::npos) {
            break;
        }
        const auto quote = blob.find('\'', next);
        if (quote == std::string_view::npos) {
            break;
        }
        const auto end_quote = blob.find('\'', quote + 1U);
        if (end_quote == std::string_view::npos) {
            break;
        }
        const auto ident = blob.substr(quote + 1U, end_quote - quote - 1U);
        if (ident.starts_with("min16float") || ident.starts_with("min16uint") ||
            ident.starts_with("min16int") || ident == "min10float" ||
            ident.starts_with("min12int")) {
            saw_min16 = true;
        } else {
            saw_non_min16_undef = true;
        }
        pos = end_quote + 1U;
    }
    return saw_min16 && !saw_non_min16_undef;
}

/// Build a `Diagnostic` describing a Slang failure. The diagnostic anchors at
/// `(source, byte 0..0)` because Slang's diagnostic blob is plain text (it
/// embeds line/col info, but parsing it back into a precise span is left for
/// a follow-up). The full Slang text is included in the message.
///
/// Severity downgrades from Error to Note (LSP "Information") when the
/// failure is *only* DXC minimum-precision types Slang doesn't recognise.
/// Real Slang errors (syntax errors, missing entry points, undeclared user
/// identifiers, ...) keep Severity::Error so they still surface loudly.
[[nodiscard]] Diagnostic make_reflection_error(SourceId source, std::string message) {
    const bool min_precision_only = is_min_precision_only_failure(message);
    Diagnostic diag;
    diag.code = std::string{"clippy::reflection"};
    diag.severity = min_precision_only ? Severity::Note : Severity::Error;
    diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    if (min_precision_only) {
        diag.message =
            std::string{
                "Slang reflection skipped for this file: it uses DXC minimum-"
                "precision types (`min16float` / `min16uint` / `min16int`) which "
                "Slang's HLSL frontend doesn't accept. AST-only rules still ran. "
                "Migrate to `float16_t` / `uint16_t` / `int16_t` to enable "
                "reflection-aware rules. (Slang detail: "} +
            std::move(message) + ")";
    } else {
        diag.message = std::move(message);
    }
    return diag;
}

/// Map Slang's stage enum to the lowercase stage tag used in
/// `EntryPointInfo::stage`.
[[nodiscard]] std::string stage_name(SlangStage stage) noexcept {
    switch (stage) {
        case SLANG_STAGE_VERTEX:
            return "vertex";
        case SLANG_STAGE_HULL:
            return "hull";
        case SLANG_STAGE_DOMAIN:
            return "domain";
        case SLANG_STAGE_GEOMETRY:
            return "geometry";
        case SLANG_STAGE_FRAGMENT:
            return "pixel";
        case SLANG_STAGE_COMPUTE:
            return "compute";
        case SLANG_STAGE_RAY_GENERATION:
            return "raygeneration";
        case SLANG_STAGE_INTERSECTION:
            return "intersection";
        case SLANG_STAGE_ANY_HIT:
            return "anyhit";
        case SLANG_STAGE_CLOSEST_HIT:
            return "closesthit";
        case SLANG_STAGE_MISS:
            return "miss";
        case SLANG_STAGE_CALLABLE:
            return "callable";
        case SLANG_STAGE_MESH:
            return "mesh";
        case SLANG_STAGE_AMPLIFICATION:
            return "amplification";
        default:
            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// DXGI format extraction (v1.2, ADR 0019).
//
// Slang's reflection exposes the typed-resource template argument via
// `TypeReflection::getResourceResultType()` -- e.g. for `Texture2D<float4>`
// that returns the `float4` type. We map (vector_size, scalar_type,
// qualifier) onto the corresponding DXGI_FORMAT_* string. Slang doesn't
// surface a first-class UNORM/SNORM bit through this API path, so we probe
// the type name (`getName()`) for `"unorm"` / `"snorm"` substrings -- HLSL
// preserves those qualifiers in the textual type rendering for typed
// resources, e.g. `Texture2D<unorm float4>` reflects with name
// `unorm float4`.
//
// This is best-effort: any case we can't classify returns an empty string,
// matching the public-header contract. v1.2 consumers handle "" by falling
// back to AST-side heuristics.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string format_for_components(unsigned components,
                                                slang::TypeReflection::ScalarType scalar,
                                                bool unorm,
                                                bool snorm) {
    using ST = slang::TypeReflection::ScalarType;

    // UNORM / SNORM are only defined on 8- and 16-bit integer scalars.
    // Any other combination falls back to plain integer / float formats.
    const bool norm = unorm || snorm;
    const bool is8 = scalar == ST::UInt8 || scalar == ST::Int8;
    const bool is16 = scalar == ST::UInt16 || scalar == ST::Int16;

    auto suffix = [&]() -> std::string {
        if (norm && (is8 || is16)) {
            return unorm ? "_UNORM" : "_SNORM";
        }
        switch (scalar) {
            case ST::Float16:
                return "_FLOAT";
            case ST::Float32:
                return "_FLOAT";
            case ST::UInt32:
                return "_UINT";
            case ST::Int32:
                return "_SINT";
            case ST::UInt16:
                return "_UINT";
            case ST::Int16:
                return "_SINT";
            case ST::UInt8:
                return "_UINT";
            case ST::Int8:
                return "_SINT";
            default:
                return std::string{};
        }
    }();
    if (suffix.empty()) {
        return std::string{};
    }

    auto bits = [&]() -> unsigned {
        switch (scalar) {
            case ST::Float16:
            case ST::UInt16:
            case ST::Int16:
                return 16U;
            case ST::Float32:
            case ST::UInt32:
            case ST::Int32:
                return 32U;
            case ST::UInt8:
            case ST::Int8:
                return 8U;
            default:
                return 0U;
        }
    }();
    if (bits == 0U) {
        return std::string{};
    }

    // Pick the DXGI channel layout. We only emit canonical N-channel layouts
    // here (R, RG, RGBA). RGB-only DXGI formats are deliberately omitted —
    // there is no `DXGI_FORMAT_R32G32B32_*` for 16- or 8-bit width, and the
    // 32-bit-per-channel `R32G32B32_FLOAT` is the sole legal triple, which
    // we do surface.
    std::string channels;
    switch (components) {
        case 1: {
            const std::string b = std::to_string(bits);
            channels = "R" + b;
            break;
        }
        case 2: {
            const std::string b = std::to_string(bits);
            channels = "R" + b + "G" + b;
            break;
        }
        case 3: {
            // RGB only valid at 32 bits per channel.
            if (bits != 32U) {
                return std::string{};
            }
            channels = "R32G32B32";
            break;
        }
        case 4: {
            const std::string b = std::to_string(bits);
            channels = "R" + b + "G" + b + "B" + b + "A" + b;
            break;
        }
        default:
            return std::string{};
    }

    return std::string{"DXGI_FORMAT_"} + channels + suffix;
}

/// Extract a DXGI_FORMAT_* string from the typed-resource template arg of
/// `resource_type`. Returns an empty string if the resource is untyped or
/// the format cannot be classified. This is best-effort; the public-header
/// contract documents that empty is a valid result.
[[nodiscard]] std::string extract_dxgi_format(slang::TypeReflection* resource_type) {
    if (resource_type == nullptr) {
        return std::string{};
    }
    if (resource_type->getKind() != slang::TypeReflection::Kind::Resource) {
        return std::string{};
    }

    // Untyped buffers don't have a meaningful DXGI format.
    const SlangResourceShape shape = resource_type->getResourceShape();
    const auto base_shape = static_cast<unsigned>(shape) & SLANG_RESOURCE_BASE_SHAPE_MASK;
    if (base_shape == SLANG_BYTE_ADDRESS_BUFFER || base_shape == SLANG_STRUCTURED_BUFFER ||
        base_shape == SLANG_ACCELERATION_STRUCTURE) {
        return std::string{};
    }

    slang::TypeReflection* element = resource_type->getResourceResultType();
    if (element == nullptr) {
        return std::string{};
    }

    // Probe the textual rendering for UNORM / SNORM qualifiers. HLSL keeps
    // those words in the typed-resource template arg's name string for the
    // versions of Slang we ship against.
    bool unorm = false;
    bool snorm = false;
    if (const char* raw = element->getName(); raw != nullptr) {
        std::string_view nm{raw};
        if (nm.find("unorm") != std::string_view::npos) {
            unorm = true;
        } else if (nm.find("snorm") != std::string_view::npos) {
            snorm = true;
        }
    }

    // Determine vector width + scalar type. Vectors expose getElementCount();
    // scalars use 1.
    unsigned components = 1U;
    slang::TypeReflection* scalar_carrier = element;
    if (element->getKind() == slang::TypeReflection::Kind::Vector) {
        const std::size_t cnt = element->getElementCount();
        if (cnt == 0U || cnt > 4U) {
            return std::string{};
        }
        components = static_cast<unsigned>(cnt);
        if (slang::TypeReflection* inner = element->getElementType(); inner != nullptr) {
            scalar_carrier = inner;
        }
    }
    const auto scalar = scalar_carrier->getScalarType();
    if (scalar == slang::TypeReflection::ScalarType::None ||
        scalar == slang::TypeReflection::ScalarType::Void) {
        return std::string{};
    }

    return format_for_components(components, scalar, unorm, snorm);
}

/// Map Slang's TypeReflection (resource) to our coarse-grained ResourceKind.
/// Slang exposes a kind tag plus a resource shape; we project both into the
/// public enum. Anything we cannot classify becomes `Unknown` -- rules then
/// decide whether to skip or warn.
[[nodiscard]] ResourceKind classify_resource(slang::TypeReflection* type) noexcept {
    if (type == nullptr) {
        return ResourceKind::Unknown;
    }
    const auto kind = type->getKind();
    switch (kind) {
        case slang::TypeReflection::Kind::SamplerState: {
            // Slang doesn't separately surface ComparisonState in TypeReflection
            // kinds; comparison samplers fall under the same kind in 2026.7.
            // We map both to SamplerState. Comparison-sampler-specific rules
            // can disambiguate via descriptor inspection (Phase 3b utilities).
            return ResourceKind::SamplerState;
        }
        case slang::TypeReflection::Kind::ConstantBuffer:
            return ResourceKind::ConstantBuffer;
        case slang::TypeReflection::Kind::Resource:
            break;  // fall through to shape switch below
        default:
            return ResourceKind::Unknown;
    }

    const SlangResourceShape shape = type->getResourceShape();
    const SlangResourceAccess access = type->getResourceAccess();
    const bool readwrite = access == SLANG_RESOURCE_ACCESS_READ_WRITE ||
                           access == SLANG_RESOURCE_ACCESS_APPEND ||
                           access == SLANG_RESOURCE_ACCESS_CONSUME;

    const auto shape_bits = static_cast<unsigned>(shape);
    const auto base_shape = shape_bits & SLANG_RESOURCE_BASE_SHAPE_MASK;
    const bool is_array = (shape_bits & SLANG_TEXTURE_ARRAY_FLAG) != 0U;
    const bool is_feedback = (shape_bits & SLANG_TEXTURE_FEEDBACK_FLAG) != 0U;

    if (is_feedback) {
        return is_array ? ResourceKind::FeedbackTexture2DArray : ResourceKind::FeedbackTexture2D;
    }

    switch (base_shape) {
        case SLANG_TEXTURE_1D:
            if (is_array) {
                return readwrite ? ResourceKind::RWTexture1DArray : ResourceKind::Texture1DArray;
            }
            return readwrite ? ResourceKind::RWTexture1D : ResourceKind::Texture1D;
        case SLANG_TEXTURE_2D:
            if (is_array) {
                return readwrite ? ResourceKind::RWTexture2DArray : ResourceKind::Texture2DArray;
            }
            return readwrite ? ResourceKind::RWTexture2D : ResourceKind::Texture2D;
        case SLANG_TEXTURE_3D:
            return readwrite ? ResourceKind::RWTexture3D : ResourceKind::Texture3D;
        case SLANG_TEXTURE_CUBE:
            return is_array ? ResourceKind::TextureCubeArray : ResourceKind::TextureCube;
        case SLANG_STRUCTURED_BUFFER:
            switch (access) {
                case SLANG_RESOURCE_ACCESS_READ:
                    return ResourceKind::StructuredBuffer;
                case SLANG_RESOURCE_ACCESS_APPEND:
                    return ResourceKind::AppendStructuredBuffer;
                case SLANG_RESOURCE_ACCESS_CONSUME:
                    return ResourceKind::ConsumeStructuredBuffer;
                case SLANG_RESOURCE_ACCESS_READ_WRITE:
                default:
                    return ResourceKind::RWStructuredBuffer;
            }
        case SLANG_BYTE_ADDRESS_BUFFER:
            return readwrite ? ResourceKind::RWByteAddressBuffer : ResourceKind::ByteAddressBuffer;
        case SLANG_TEXTURE_BUFFER:
            return readwrite ? ResourceKind::RWBuffer : ResourceKind::Buffer;
        case SLANG_ACCELERATION_STRUCTURE:
            return ResourceKind::AccelerationStructure;
        default:
            return ResourceKind::Unknown;
    }
}

/// Render a TypeReflection's name into a string suitable for diagnostic
/// rendering. Falls back to a generic placeholder when Slang cannot give us
/// a name (rare).
[[nodiscard]] std::string type_name_string(slang::TypeReflection* type) {
    if (type == nullptr) {
        return std::string{"<unknown>"};
    }
    const char* raw = type->getName();
    if (raw == nullptr) {
        return std::string{"<anonymous>"};
    }
    return std::string{raw};
}

/// Walk a Slang VariableLayoutReflection that represents a constant-buffer
/// type and project its element layout into a CBufferLayout.
void populate_cbuffer_layout(const std::string& cbuffer_name,
                             slang::VariableLayoutReflection* var_layout,
                             SourceId source,
                             CBufferLayout& out) {
    out.name = cbuffer_name;
    out.declaration_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    out.fields.clear();
    out.total_bytes = 0U;

    if (var_layout == nullptr) {
        return;
    }
    slang::TypeLayoutReflection* outer_layout = var_layout->getTypeLayout();
    if (outer_layout == nullptr) {
        return;
    }

    // For a ConstantBuffer<T>, the outer layout's element type carries the
    // member fields and total size.
    slang::TypeLayoutReflection* element_layout = outer_layout->getElementTypeLayout();
    if (element_layout == nullptr) {
        element_layout = outer_layout;
    }

    out.total_bytes =
        static_cast<std::uint32_t>(element_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));

    const unsigned int field_count = element_layout->getFieldCount();
    out.fields.reserve(field_count);
    for (unsigned int i = 0; i < field_count; ++i) {
        slang::VariableLayoutReflection* field = element_layout->getFieldByIndex(i);
        if (field == nullptr) {
            continue;
        }
        CBufferField cf;
        const char* field_name = field->getName();
        cf.name = field_name != nullptr ? std::string{field_name} : std::string{"<anonymous>"};
        cf.byte_offset =
            static_cast<std::uint32_t>(field->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM));
        slang::TypeLayoutReflection* field_layout = field->getTypeLayout();
        if (field_layout != nullptr) {
            cf.byte_size =
                static_cast<std::uint32_t>(field_layout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM));
            cf.type_name = type_name_string(field_layout->getType());
        } else {
            cf.byte_size = 0U;
            cf.type_name = std::string{"<unknown>"};
        }
        out.fields.push_back(std::move(cf));
    }
}

/// Walk a Slang VariableLayoutReflection that represents a top-level shader
/// parameter (resource or cbuffer) and emit either a ResourceBinding or a
/// CBufferLayout into the appropriate output vector.
void emit_parameter(slang::VariableLayoutReflection* param,
                    SourceId source,
                    std::vector<ResourceBinding>& bindings,
                    std::vector<CBufferLayout>& cbuffers) {
    if (param == nullptr) {
        return;
    }
    slang::TypeLayoutReflection* type_layout = param->getTypeLayout();
    if (type_layout == nullptr) {
        return;
    }
    slang::TypeReflection* type = type_layout->getType();

    const char* raw_name = param->getName();
    const std::string name = raw_name != nullptr ? std::string{raw_name} : std::string{};

    const auto kind = type != nullptr ? type->getKind() : slang::TypeReflection::Kind::None;

    if (kind == slang::TypeReflection::Kind::ConstantBuffer ||
        kind == slang::TypeReflection::Kind::ParameterBlock) {
        CBufferLayout cb;
        populate_cbuffer_layout(name, param, source, cb);
        cbuffers.push_back(std::move(cb));

        // ConstantBuffer also occupies a `b#` slot; surface it as a binding so
        // root-signature-shape rules can see it alongside textures / samplers.
        ResourceBinding rb;
        rb.name = name;
        rb.kind = ResourceKind::ConstantBuffer;
        rb.register_slot =
            static_cast<std::uint32_t>(param->getOffset(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER));
        rb.register_space = static_cast<std::uint32_t>(
            param->getBindingSpace(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER));
        rb.declaration_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        bindings.push_back(std::move(rb));
        return;
    }

    // For SRVs / UAVs / Samplers Slang reports the binding via the appropriate
    // category. Probe the most specific category first; fall back to a generic
    // descriptor-table-slot category when Slang reports nothing.
    auto category = static_cast<SlangParameterCategory>(type_layout->getParameterCategory());

    ResourceBinding rb;
    rb.name = name;
    rb.kind = classify_resource(type);
    rb.register_slot = static_cast<std::uint32_t>(param->getOffset(category));
    rb.register_space = static_cast<std::uint32_t>(param->getBindingSpace(category));

    // Resource type used for DXGI-format extraction. For arrays of resources
    // (e.g. `Texture2D<float4> tex_array[]`) we want the element type, not
    // the array wrapper.
    slang::TypeReflection* format_carrier = type;

    // Slang reports unbounded arrays via the type-layout's element count.
    if (type != nullptr && type->getKind() == slang::TypeReflection::Kind::Array) {
        const std::size_t count = type->getElementCount();
        if (count != 0U) {
            rb.array_size = static_cast<std::uint32_t>(count);
        }
        // Promote the inner type to classify the resource correctly.
        slang::TypeReflection* element_type = type->getElementType();
        if (element_type != nullptr) {
            rb.kind = classify_resource(element_type);
            format_carrier = element_type;
        }
    }

    // v1.2 (ADR 0019) — surface the typed-resource DXGI format. The helper
    // returns "" for untyped buffers / samplers / accel-structs, which is
    // also the default-constructed value, so a bare assignment is correct.
    rb.dxgi_format = extract_dxgi_format(format_carrier);

    rb.declaration_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    bindings.push_back(std::move(rb));
}

/// Default profile string when the caller didn't override. We pick `sm_6_6`
/// as the floor matching the Phase 3 ADR; per-stage variants are derived in
/// `derive_profile()` below from each entry-point's stage.
constexpr std::string_view k_default_profile = "sm_6_6";

}  // namespace

// ---------------------------------------------------------------------------
// SlangBridge::Impl: holds the IGlobalSession + ISession pool. The PIMPL
// keeps `<slang.h>` strictly inside this TU.
// ---------------------------------------------------------------------------

struct SlangBridge::Impl {
    Slang::ComPtr<slang::IGlobalSession> global_session;

    std::mutex pool_mu;
    struct SessionEntry {
        Slang::ComPtr<slang::ISession> session;
        std::string target_profile;
        std::string include_key;
    };
    std::vector<SessionEntry> session_pool;
    std::uint32_t pool_size_cap = 4U;

    explicit Impl(std::uint32_t pool_size) : pool_size_cap(pool_size == 0U ? 1U : pool_size) {
        // IGlobalSession creation is documented as expensive but is the
        // cheapest way to discover the host Slang library. CLAUDE.md notes
        // it is not thread-safe; we never mutate it post-construction.
        const SlangResult res = slang::createGlobalSession(global_session.writeRef());
        if (SLANG_FAILED(res)) {
            global_session = nullptr;
        }
    }

    /// Acquire a session from the pool, creating one on demand. Returns null
    /// when the global session is unavailable or session creation failed.
    [[nodiscard]] Slang::ComPtr<slang::ISession> acquire(
        std::string_view target_profile,
        std::span<const std::filesystem::path> include_directories) {
        Slang::ComPtr<slang::ISession> session;
        if (global_session == nullptr) {
            return session;
        }
        std::vector<std::string> search_path_storage;
        search_path_storage.reserve(include_directories.size());
        std::string include_key;
        for (const auto& dir : include_directories) {
            const std::string path = dir.lexically_normal().string();
            if (!include_key.empty()) {
                include_key.push_back('\n');
            }
            include_key += path;
            search_path_storage.push_back(path);
        }
        const std::string profile_string{target_profile};
        {
            std::lock_guard<std::mutex> lock(pool_mu);
            for (auto it = session_pool.begin(); it != session_pool.end(); ++it) {
                if (it->target_profile == profile_string && it->include_key == include_key) {
                    session = std::move(it->session);
                    session_pool.erase(it);
                    return session;
                }
            }
        }

        // Pool empty -- create a new session targeting DXIL with the
        // requested profile.
        slang::TargetDesc target_desc{};
        target_desc.format = SLANG_DXIL;
        target_desc.profile = global_session->findProfile(profile_string.c_str());
        if (target_desc.profile == SLANG_PROFILE_UNKNOWN) {
            target_desc.profile =
                global_session->findProfile(std::string{k_default_profile}.c_str());
        }

        slang::SessionDesc session_desc{};
        session_desc.targets = &target_desc;
        session_desc.targetCount = 1;
        std::vector<const char*> search_paths;
        search_paths.reserve(search_path_storage.size());
        for (const auto& path : search_path_storage) {
            search_paths.push_back(path.c_str());
        }
        session_desc.searchPaths = search_paths.empty() ? nullptr : search_paths.data();
        session_desc.searchPathCount = static_cast<SlangInt>(search_paths.size());

        const SlangResult res = global_session->createSession(session_desc, session.writeRef());
        if (SLANG_FAILED(res)) {
            session = nullptr;
        }
        return session;
    }

    /// Return a session to the pool when there's headroom, otherwise let it
    /// drop (the ComPtr destructor will release).
    void release(Slang::ComPtr<slang::ISession> session,
                 std::string target_profile,
                 std::string include_key) {
        if (session == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> lock(pool_mu);
        if (session_pool.size() < pool_size_cap) {
            session_pool.push_back(SessionEntry{
                .session = std::move(session),
                .target_profile = std::move(target_profile),
                .include_key = std::move(include_key),
            });
        }
    }
};

SlangBridge::SlangBridge(std::uint32_t pool_size) : impl_(std::make_unique<Impl>(pool_size)) {}
SlangBridge::~SlangBridge() = default;

std::expected<ReflectionInfo, Diagnostic> SlangBridge::reflect(
    const SourceManager& sources,
    SourceId source,
    std::string_view target_profile,
    std::span<const std::filesystem::path> include_directories) {
    const SourceFile* file = sources.get(source);
    if (file == nullptr) {
        return std::unexpected{make_reflection_error(source, std::string{"unknown source id"})};
    }
    if (impl_->global_session == nullptr) {
        return std::unexpected{make_reflection_error(
            source, std::string{"failed to initialise Slang global session"})};
    }

    const std::string profile =
        target_profile.empty() ? std::string{k_default_profile} : std::string{target_profile};
    std::string include_key;
    for (const auto& dir : include_directories) {
        if (!include_key.empty()) {
            include_key.push_back('\n');
        }
        include_key += dir.lexically_normal().string();
    }

    Slang::ComPtr<slang::ISession> session = impl_->acquire(profile, include_directories);
    if (session == nullptr) {
        return std::unexpected{make_reflection_error(
            source, std::string{"failed to acquire Slang ISession from pool"})};
    }

    // RAII guard: always return the session to the pool, even on early exit.
    // The guard borrows `session` through a pointer so the original ComPtr
    // remains usable for the rest of the function; on destruction it moves
    // the session out of that pointer and hands it back to the pool.
    struct Releaser {
        Impl* impl;
        Slang::ComPtr<slang::ISession>* session_slot;
        std::string target_profile;
        std::string include_key;
        Releaser(Impl* i,
                 Slang::ComPtr<slang::ISession>* s,
                 std::string profile_in,
                 std::string include_key_in)
            : impl(i),
              session_slot(s),
              target_profile(std::move(profile_in)),
              include_key(std::move(include_key_in)) {}
        Releaser(const Releaser&) = delete;
        Releaser& operator=(const Releaser&) = delete;
        Releaser(Releaser&&) = delete;
        Releaser& operator=(Releaser&&) = delete;
        ~Releaser() {
            impl->release(
                std::move(*session_slot), std::move(target_profile), std::move(include_key));
        }
    };
    [[maybe_unused]] Releaser releaser{
        impl_.get(), std::addressof(session), profile, std::move(include_key)};

    // Load the source as a module.
    //
    // Slang's `ISession` caches modules by name AND by virtual path:
    // `loadModuleFromSourceString` with the same `module_name` or the same
    // virtual `path` (used as the file-system key) returns the previously-
    // loaded module's reflection regardless of the new source contents -- and
    // re-loading the same name + path with different contents triggers a
    // "key already exists in Dictionary" assert inside Slang. To prevent
    // stale reflection bleeding between unrelated lint calls (e.g. multiple
    // unit tests in the same process, each using `synthetic.hlsl`), every
    // reflect call gets a process-unique module name AND virtual-path suffix.
    // This is essential for correctness, not just hygiene -- without it a
    // `RWStructuredBuffer`-bound shader will inherit a previous shader's
    // `ByteAddressBuffer` reflection.
    //
    // ADR 0020 sub-phase A v1.3.1 fix: previously the call-suffix was
    // appended verbatim to the END of `virtual_path`, e.g. `synthetic.hlsl`
    // became `synthetic.hlsl__7`. Slang 2026.7.1 infers source-language
    // from `virtual_path`'s file extension (`.hlsl` -> HLSL frontend,
    // `.slang` -> Slang frontend). The trailing `__N` corrupted the
    // extension; for `.hlsl` paths Slang's parser still happened to fall
    // back to HLSL by name-prefix match, but for `.slang` paths the
    // frontend ingestion crashed inside `loadModuleFromSourceString`. The
    // fix splices the call counter BEFORE the extension instead of after
    // it: `synthetic.hlsl` -> `synthetic__7.hlsl`, `foo.slang` ->
    // `foo__7.slang`. Both Slang frontends now classify correctly, and
    // the path remains process-unique so the module-dictionary collision
    // covered by `[reflection][engine][regression]` is still avoided.
    static std::atomic<std::uint64_t> s_call_counter{0U};
    const std::uint64_t call_id = s_call_counter.fetch_add(1U, std::memory_order_relaxed);
    const std::string call_suffix = std::string{"__"} + std::to_string(call_id);

    const std::string contents{file->contents()};
    const std::string base_module_name = file->path().stem().string().empty()
                                             ? std::string{"shader_clippy_module"}
                                             : file->path().stem().string();
    const std::string module_name = base_module_name + call_suffix;

    // Splice the per-call suffix BEFORE the extension so Slang's extension-
    // based source-language inference still matches `.hlsl` / `.slang`.
    // For path-less sources we fall back to a `<buffer>` stem with the
    // counter appended (no extension is present to disrupt language
    // inference, and the `<buffer>` form is what Slang already saw pre-fix
    // for path-less inputs).
    std::string virtual_path;
    {
        const std::string raw_path = file->path().string();
        if (raw_path.empty()) {
            virtual_path = std::string{"<buffer>"} + call_suffix;
        } else {
            const std::string stem = file->path().stem().string();
            const std::string ext = file->path().extension().string();
            const std::string parent = file->path().parent_path().string();
            std::string filename = stem + call_suffix + ext;
            if (parent.empty()) {
                virtual_path = std::move(filename);
            } else {
                virtual_path = parent + std::string{"/"} + filename;
            }
        }
    }

    Slang::ComPtr<slang::IBlob> load_diag;
    slang::IModule* raw_module = session->loadModuleFromSourceString(
        module_name.c_str(), virtual_path.c_str(), contents.c_str(), load_diag.writeRef());
    if (raw_module == nullptr) {
        std::string msg = std::string{"slang load failed: "} + blob_to_string(load_diag.get());
        return std::unexpected{make_reflection_error(source, std::move(msg))};
    }
    Slang::ComPtr<slang::IModule> module_ptr;
    module_ptr.attach(raw_module);
    raw_module->addRef();

    // Discover all entry points the module exposes via its `[shader(...)]`
    // attributes. Each becomes one EntryPointInfo.
    const std::int32_t entry_point_count = module_ptr->getDefinedEntryPointCount();
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entry_point_objs;
    entry_point_objs.reserve(
        static_cast<std::size_t>(entry_point_count > 0 ? entry_point_count : 0));
    for (std::int32_t i = 0; i < entry_point_count; ++i) {
        Slang::ComPtr<slang::IEntryPoint> ep;
        const SlangResult res = module_ptr->getDefinedEntryPoint(i, ep.writeRef());
        if (SLANG_FAILED(res) || ep == nullptr) {
            continue;
        }
        entry_point_objs.push_back(std::move(ep));
    }

    // Build a composite component containing the module + every entry point so
    // that linked reflection sees them all.
    std::vector<slang::IComponentType*> components;
    components.reserve(1U + entry_point_objs.size());
    components.push_back(module_ptr.get());
    for (auto& ep : entry_point_objs) {
        components.push_back(ep.get());
    }

    Slang::ComPtr<slang::IComponentType> composite;
    {
        Slang::ComPtr<slang::IBlob> compose_diag;
        const SlangResult res =
            session->createCompositeComponentType(components.data(),
                                                  static_cast<SlangInt>(components.size()),
                                                  composite.writeRef(),
                                                  compose_diag.writeRef());
        if (SLANG_FAILED(res) || composite == nullptr) {
            std::string msg =
                std::string{"slang composite failed: "} + blob_to_string(compose_diag.get());
            return std::unexpected{make_reflection_error(source, std::move(msg))};
        }
    }

    Slang::ComPtr<slang::IComponentType> linked;
    {
        Slang::ComPtr<slang::IBlob> link_diag;
        const SlangResult res = composite->link(linked.writeRef(), link_diag.writeRef());
        if (SLANG_FAILED(res) || linked == nullptr) {
            std::string msg = std::string{"slang link failed: "} + blob_to_string(link_diag.get());
            return std::unexpected{make_reflection_error(source, std::move(msg))};
        }
    }

    slang::ProgramLayout* program_layout = linked->getLayout(0);
    if (program_layout == nullptr) {
        return std::unexpected{
            make_reflection_error(source, std::string{"slang reflection produced no layout"})};
    }

    ReflectionInfo info;
    info.target_profile = profile;

    // Top-level shader parameters: SRVs / UAVs / samplers / cbuffers.
    const unsigned int param_count = program_layout->getParameterCount();
    info.bindings.reserve(param_count);
    for (unsigned int i = 0; i < param_count; ++i) {
        slang::VariableLayoutReflection* param = program_layout->getParameterByIndex(i);
        emit_parameter(param, source, info.bindings, info.cbuffers);
    }

    // Entry points: one EntryPointInfo per `[shader(...)]` declaration.
    const auto reflected_ep_count = static_cast<unsigned int>(program_layout->getEntryPointCount());
    info.entry_points.reserve(reflected_ep_count);
    for (unsigned int i = 0; i < reflected_ep_count; ++i) {
        slang::EntryPointReflection* ep = program_layout->getEntryPointByIndex(i);
        if (ep == nullptr) {
            continue;
        }
        EntryPointInfo epi;
        const char* raw_name = ep->getName();
        epi.name = raw_name != nullptr ? std::string{raw_name} : std::string{"<anonymous>"};
        epi.stage = stage_name(ep->getStage());

        // Slang's API takes a `SlangUInt[]` out-pointer; std::array decays
        // cleanly so we pass `.data()` to keep clang-tidy's
        // `cppcoreguidelines-avoid-c-arrays` happy without losing the
        // contiguous-storage interop.
        std::array<SlangUInt, 3> thread_group{0U, 0U, 0U};
        ep->getComputeThreadGroupSize(3, thread_group.data());
        if (thread_group[0] != 0U || thread_group[1] != 0U || thread_group[2] != 0U) {
            epi.numthreads = std::array<std::uint32_t, 3>{
                static_cast<std::uint32_t>(thread_group[0]),
                static_cast<std::uint32_t>(thread_group[1]),
                static_cast<std::uint32_t>(thread_group[2]),
            };
        }

        // Wave-size reflection is intentionally deferred to a Phase 3b
        // shared-utility per ADR 0012 §6 (`reflect_stage.hpp`). Slang's
        // wave-size accessor surface has churned across recent versions;
        // we leave the optionals empty here so reflection-aware rules can
        // detect "not reflected yet" without crashing.

        epi.declaration_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        info.entry_points.push_back(std::move(epi));
    }

    return info;
}

}  // namespace shader_clippy::reflection
