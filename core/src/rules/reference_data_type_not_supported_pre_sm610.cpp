// reference-data-type-not-supported-pre-sm610
//
// Defensive rule for HLSL specs proposal 0006 (`reference` data types,
// under-review). Detects `inout` / `out` / `in ref` patterns that look
// like reference-type usage and warns when targeting `sm_6_9` or older
// because reference types aren't shipped retail yet.
//
// Stage: Reflection. Suggestion-grade.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "reference-data-type-not-supported-pre-sm610";
constexpr std::string_view k_category = "sm6_10";

class ReferenceDataTypeNotSupportedPreSm610 : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        // Always fire on the source pattern. The doc page explains the
        // SM 6.10 horizon -- authors who already target SM 6.10 retail
        // should suppress the diagnostic inline. Reflection-driven gating
        // would require Slang to compile the source, which the proposal-
        // 0006 syntax doesn't yet round-trip cleanly.
        const auto bytes = tree.source_bytes();
        const auto pos = bytes.find(" ref ");
        if (pos == std::string_view::npos) {
            return;
        }
        // Confirm the prefix is one of the parameter qualifiers.
        std::size_t k = pos;
        while (k > 0U && (bytes[k - 1U] == ' ' || bytes[k - 1U] == '\t' || bytes[k - 1U] == 'a' ||
                          bytes[k - 1U] == 'b' || bytes[k - 1U] == 'c' || bytes[k - 1U] == 'd' ||
                          bytes[k - 1U] == 'e' || bytes[k - 1U] == 'f' || bytes[k - 1U] == 'g' ||
                          bytes[k - 1U] == 'h' || bytes[k - 1U] == 'i' || bytes[k - 1U] == 'j' ||
                          bytes[k - 1U] == 'k' || bytes[k - 1U] == 'l' || bytes[k - 1U] == 'm' ||
                          bytes[k - 1U] == 'n' || bytes[k - 1U] == 'o' || bytes[k - 1U] == 'p' ||
                          bytes[k - 1U] == 'q' || bytes[k - 1U] == 'r' || bytes[k - 1U] == 's' ||
                          bytes[k - 1U] == 't' || bytes[k - 1U] == 'u' || bytes[k - 1U] == 'v' ||
                          bytes[k - 1U] == 'w' || bytes[k - 1U] == 'x' || bytes[k - 1U] == 'y' ||
                          bytes[k - 1U] == 'z')) {
            --k;
        }
        const auto prefix = bytes.substr(k, pos - k);
        const bool is_param_qual = prefix.find("inout") != std::string_view::npos ||
                                   prefix.find("out") != std::string_view::npos ||
                                   prefix.find("in") != std::string_view::npos;
        if (!is_param_qual) {
            return;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{
            .source = tree.source_id(),
            .bytes = ByteSpan{static_cast<std::uint32_t>(pos),
                              static_cast<std::uint32_t>(pos + std::string_view{" ref "}.size())},
        };
        diag.message =
            "(suggestion) `<qual> ref <type>` parameter syntax matches HLSL specs "
            "proposal 0006 (reference data types) which is still under-review; "
            "reference types are not shipped retail on SM <= 6.9, and the "
            "syntax may move before SM 6.10 retail";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_reference_data_type_not_supported_pre_sm610() {
    return std::make_unique<ReferenceDataTypeNotSupportedPreSm610>();
}

}  // namespace hlsl_clippy::rules
