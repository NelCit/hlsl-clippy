// min16float-in-cbuffer-roundtrip
//
// Detects shader code that loads a `min16float` (or `half`) parameter from
// a 32-bit cbuffer field. The HLSL packing rule promotes 16-bit cbuffer
// fields to 32 bits unless the shader is compiled with the `-enable-16bit-
// types` flag; with that flag the cbuffer field stays at 16 bits but the
// load incurs a 32 -> 16 conversion on every read.
//
// Detection plan: walk reflection cbuffers. For every field whose type_name
// starts with `half` or `min16` AND the cbuffer is not declared with a
// `[allow_16bit_types]`-style annotation, emit. Today the bridge surfaces
// type names like "half", "half3", "min16float", "min16int" -- the rule
// fires on those.

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

constexpr std::string_view k_rule_id = "min16float-in-cbuffer-roundtrip";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool type_is_16bit(std::string_view type_name) noexcept {
    if (type_name.starts_with("half"))
        return true;
    if (type_name.starts_with("min16"))
        return true;
    return false;
}

class Min16FloatInCBufferRoundtrip : public Rule {
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
                if (!type_is_16bit(field.type_name))
                    continue;
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span = cb.declaration_span;
                diag.message = std::string{"cbuffer field `"} + cb.name + "." + field.name +
                               "` is `" + field.type_name +
                               "` -- without `-enable-16bit-types` the cbuffer load is promoted "
                               "to 32 bits, then converted back on every read; with the flag the "
                               "field stays at 16 bits but every load still pays a 32->16 cast";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_min16float_in_cbuffer_roundtrip() {
    return std::make_unique<Min16FloatInCBufferRoundtrip>();
}

}  // namespace hlsl_clippy::rules
