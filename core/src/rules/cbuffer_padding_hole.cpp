// cbuffer-padding-hole
//
// Detects implicit padding gaps inside `cbuffer` / `ConstantBuffer<T>`
// declarations. HLSL packing places each member on its natural alignment
// (capped at 16 bytes) and never lets a member straddle a 16-byte slot
// boundary, so a `float` followed by a `float3` leaves a 12-byte hole.
//
// Detection plan: walk every CBufferLayout's fields in declared order; for
// each adjacent pair check whether `prev.byte_offset + prev.byte_size <
// next.byte_offset`. If so, emit a diagnostic naming the hole's offset
// and size. Suggestion-only (reordering members can break CPU-side fill
// code).

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

constexpr std::string_view k_rule_id = "cbuffer-padding-hole";
constexpr std::string_view k_category = "bindings";

class CBufferPaddingHole : public Rule {
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

    void on_reflection([[maybe_unused]] const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        for (const auto& cb : reflection.cbuffers) {
            if (cb.fields.size() < 2U) {
                continue;
            }
            for (std::size_t i = 1; i < cb.fields.size(); ++i) {
                const auto& prev = cb.fields[i - 1];
                const auto& next = cb.fields[i];
                const std::uint32_t prev_end = prev.byte_offset + prev.byte_size;
                if (prev_end < next.byte_offset) {
                    const std::uint32_t hole_size = next.byte_offset - prev_end;
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = cb.declaration_span;
                    diag.message = std::string{"cbuffer `"} + cb.name + "` has a " +
                                   std::to_string(hole_size) +
                                   "-byte padding hole between fields `" + prev.name + "` and `" +
                                   next.name + "` (offset " + std::to_string(prev_end) + ".." +
                                   std::to_string(next.byte_offset) +
                                   ") -- reorder fields largest-to-smallest to reclaim cbuffer "
                                   "bandwidth";
                    ctx.emit(std::move(diag));
                }
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_cbuffer_padding_hole() {
    return std::make_unique<CBufferPaddingHole>();
}

}  // namespace shader_clippy::rules
