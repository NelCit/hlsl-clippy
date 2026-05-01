// cbuffer-fits-rootconstants
//
// Detects `cbuffer` / `ConstantBuffer<T>` declarations whose total size is at
// most 32 bytes (8 DWORDs) -- the maximum that D3D12 root constants can hold
// in one root-parameter slot. Root constants bypass the descriptor-table
// indirection on every IHV, saving a dependent load on the constant-data
// path.
//
// Detection plan: iterate `ReflectionInfo::cbuffers` and emit when
// `total_bytes` is non-zero and `<= 32`. Suggestion only -- the rewrite
// involves a CPU-side root-signature change.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "cbuffer-fits-rootconstants";
constexpr std::string_view k_category = "bindings";
constexpr std::uint32_t k_root_constant_byte_cap = 32U;  // 8 DWORDs

class CBufferFitsRootConstants : public Rule {
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
        for (const auto& cb : reflection.cbuffers) {
            if (cb.total_bytes == 0U || cb.total_bytes > k_root_constant_byte_cap) {
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = cb.declaration_span;
            diag.message = std::string{"cbuffer `"} + cb.name + "` is " +
                           std::to_string(cb.total_bytes) +
                           " bytes -- fits in a D3D12 root-constant slot (<= 32 bytes / 8 DWORDs); "
                           "promote to root constants to skip the descriptor-table indirection";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_cbuffer_fits_rootconstants() {
    return std::make_unique<CBufferFitsRootConstants>();
}

}  // namespace hlsl_clippy::rules
