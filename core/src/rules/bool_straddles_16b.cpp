// bool-straddles-16b
//
// Detects a `bool` cbuffer field that lands at a byte offset where its
// 4-byte representation either crosses or sits exactly on a 16-byte slot
// boundary in a way that is implementation-defined across `dxc` / `fxc` /
// Slang. The canonical trigger is a `float3` (12 B) followed by `bool`,
// which lands at offset 12 within the same 16-byte slot.
//
// Detection plan: iterate every `CBufferField`. When the field's `type_name`
// contains "bool" (case-insensitive) and its offset modulo 16 is in
// (0, 16), check whether the field's tail crosses the next slot boundary
// or sits exactly at offset 12 (the implementation-defined sweet spot).

#include <cctype>
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

constexpr std::string_view k_rule_id = "bool-straddles-16b";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] bool type_is_bool(std::string_view type_name) noexcept {
    // Match "bool" / "bool2" / "bool3" / "bool4" tokens at the start of the
    // type-name string. The reflection-engine renderer prefixes precision /
    // matrix qualifiers, so a contains-check is safer than a startswith.
    if (type_name.empty())
        return false;
    const auto pos = type_name.find("bool");
    if (pos == std::string_view::npos)
        return false;
    // Accept "bool" or "boolN" where N is a digit; reject "boolean" etc.
    const std::size_t after = pos + 4U;
    if (after >= type_name.size())
        return true;
    const char c = type_name[after];
    return c == '\0' || c == ' ' || c == '\t' || c == '[' || (c >= '1' && c <= '4');
}

class BoolStraddles16b : public Rule {
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
            for (const auto& field : cb.fields) {
                if (!type_is_bool(field.type_name))
                    continue;
                const std::uint32_t off_in_slot = field.byte_offset % 16U;
                if (off_in_slot == 0U)
                    continue;
                const std::uint32_t end_in_slot = off_in_slot + field.byte_size;
                const bool straddles = (end_in_slot > 16U);
                const bool tail_aligned = (end_in_slot == 16U && off_in_slot != 0U);
                if (!straddles && !tail_aligned)
                    continue;
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = cb.declaration_span};
                diag.message = std::string{"`bool` field `"} + field.name + "` in cbuffer `" +
                               cb.name + "` lands at offset " + std::to_string(field.byte_offset) +
                               " (in-slot byte " + std::to_string(off_in_slot) +
                               ") -- the layout is implementation-defined across `dxc` / `fxc` / "
                               "Slang; insert a `float` pad or reorder so the `bool` starts on a "
                               "16-byte slot";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_bool_straddles_16b() {
    return std::make_unique<BoolStraddles16b>();
}

}  // namespace hlsl_clippy::rules
