// samplecmp-vs-manual-compare
//
// Detects hand-rolled depth comparisons of the form
// `<tex>.SampleLevel(<sampler>, <uv>, 0) <op> <ref>` where `<op>` is one of
// `<` / `<=` / `>` / `>=` and the left-hand side is a depth-style sample.
// The HLSL idiom is `<tex>.SampleCmpLevelZero(<comparison-sampler>, <uv>,
// <ref>)` (or `SampleCmp` when LOD selection is needed); using
// `SampleCmp*` enables the hardware's PCF path on every IHV.
//
// Detection plan: AST scan for the literal pattern. We do not require
// reflection -- the syntactic shape ("`SampleLevel` result followed by a
// comparison operator with a scalar") is the canonical hand-rolled form.

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

constexpr std::string_view k_rule_id = "samplecmp-vs-manual-compare";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

class SampleCmpVsManualCompare : public Rule {
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
        const auto bytes = tree.source_bytes();
        constexpr std::string_view k_needle = ".Sample";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos)
                return;
            const std::size_t method_start = found + 1U;
            const std::size_t after = method_start + std::string_view{"Sample"}.size();
            // Determine the actual method name -- accept Sample, SampleLevel,
            // SampleBias. Reject SampleCmp* (already correct), SampleGrad
            // (different concern).
            std::size_t name_end = after;
            while (name_end < bytes.size() && is_id_char(bytes[name_end]))
                ++name_end;
            const auto method = bytes.substr(method_start, name_end - method_start);
            if (method != "Sample" && method != "SampleLevel" && method != "SampleBias") {
                pos = found + 1U;
                continue;
            }
            if (name_end >= bytes.size() || bytes[name_end] != '(') {
                pos = found + 1U;
                continue;
            }
            // Find the matching `)`.
            int depth = 0;
            std::size_t i = name_end;
            while (i < bytes.size()) {
                if (bytes[i] == '(')
                    ++depth;
                else if (bytes[i] == ')') {
                    --depth;
                    if (depth == 0)
                        break;
                }
                ++i;
            }
            if (i >= bytes.size()) {
                pos = found + 1U;
                continue;
            }
            // After the `)`, optionally a `.x` / `.r` swizzle, then a comparison
            // operator.
            std::size_t k = i + 1U;
            if (k < bytes.size() && bytes[k] == '.') {
                ++k;
                while (k < bytes.size() && is_id_char(bytes[k]))
                    ++k;
            }
            while (k < bytes.size() && (bytes[k] == ' ' || bytes[k] == '\t'))
                ++k;
            if (k >= bytes.size()) {
                pos = i + 1U;
                continue;
            }
            const char c = bytes[k];
            const bool is_cmp = (c == '<' || c == '>') &&
                                (k + 1U >= bytes.size() || bytes[k + 1U] != '<') &&
                                (k + 1U >= bytes.size() || bytes[k + 1U] != '>');
            if (!is_cmp) {
                pos = i + 1U;
                continue;
            }
            const auto call_lo = found + 1U;
            const auto cmp_hi = (k + 2U <= bytes.size() && bytes[k + 1U] == '=') ? k + 2U : k + 1U;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(call_lo),
                                                       static_cast<std::uint32_t>(cmp_hi)}};
            diag.message =
                std::string{"hand-rolled depth compare on a texture sample -- replace `"} +
                std::string{method} + "(...) " + std::string{1U, c} +
                " ref` with `SampleCmp` / `SampleCmpLevelZero` against a "
                "`SamplerComparisonState` to use the hardware PCF path";
            ctx.emit(std::move(diag));
            pos = cmp_hi;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_samplecmp_vs_manual_compare() {
    return std::make_unique<SampleCmpVsManualCompare>();
}

}  // namespace hlsl_clippy::rules
