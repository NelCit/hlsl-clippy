// oversized-cbuffer
//
// Detects `cbuffer` / `ConstantBuffer<T>` declarations whose total byte size
// exceeds a configurable threshold (default 4 KB). Large cbuffers blow past
// the constant cache working set on RDNA / Turing / Ada and force per-wave
// re-fetches that defeat the broadcast-per-CU benefit.
//
// Detection plan: iterate `ReflectionInfo::cbuffers` and emit when
// `total_bytes > k_threshold_bytes`. The threshold is hard-coded to 4096 in
// this initial implementation; a config knob arrives with the per-rule config
// surface.

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

constexpr std::string_view k_rule_id = "oversized-cbuffer";
constexpr std::string_view k_category = "bindings";
constexpr std::uint32_t k_threshold_bytes = 4096U;

class OversizedCBuffer : public Rule {
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
            if (cb.total_bytes > k_threshold_bytes) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = cb.declaration_span};
                diag.message = std::string{"cbuffer `"} + cb.name + "` is " +
                               std::to_string(cb.total_bytes) + " bytes (> " +
                               std::to_string(k_threshold_bytes) +
                               "-byte threshold) -- consider splitting or moving large arrays "
                               "to a `StructuredBuffer`";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_oversized_cbuffer() {
    return std::make_unique<OversizedCBuffer>();
}

}  // namespace hlsl_clippy::rules
