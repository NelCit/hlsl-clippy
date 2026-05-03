// as-payload-over-16k
//
// Detects amplification-shader payload structs whose total byte size exceeds
// the 16,384-byte (16 KB) D3D12 mesh-pipeline cap. The payload is staged
// through on-chip memory between the AS and the launched mesh-shader
// workgroups; exceeding the cap fails PSO creation.
//
// Detection plan: walk every cbuffer-like reflection cbuffer (Slang surfaces
// some payload structs through the cbuffer reflection path); when not
// available, fall back to AST struct-size summing for any struct used as the
// payload parameter of a `DispatchMesh(...)` call. This first cut emits when
// any reflected `CBufferLayout` exceeds 16 KB AND the source contains
// `DispatchMesh` / `[shader("amplification")]` markers.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "as-payload-over-16k";
constexpr std::string_view k_category = "mesh";
constexpr std::uint32_t k_cap_bytes = 16384U;

class AsPayloadOver16k : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        // Payload only matters when an amplification entry point is present
        // and DispatchMesh is called.
        const bool has_amp = bytes.find("\"amplification\"") != std::string_view::npos;
        const bool has_dm = bytes.find("DispatchMesh") != std::string_view::npos;
        if (!has_amp && !has_dm)
            return;
        for (const auto& cb : reflection.cbuffers) {
            if (cb.total_bytes <= k_cap_bytes)
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = cb.declaration_span;
            diag.message = std::string{"struct `"} + cb.name + "` is " +
                           std::to_string(cb.total_bytes) +
                           " bytes -- if used as an amplification-shader payload it exceeds the "
                           "D3D12 16384-byte cap and PSO creation will return `E_INVALIDARG`";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_as_payload_over_16k() {
    return std::make_unique<AsPayloadOver16k>();
}

}  // namespace shader_clippy::rules
