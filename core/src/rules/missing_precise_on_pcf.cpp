// missing-precise-on-pcf
//
// Detects depth-comparison arithmetic (`SampleCmp` / `SampleCmpLevelZero`
// followed by combine-with-weights math) that lacks a `precise` qualifier.
// Without `precise`, fast-math reassociation can perturb the rounding of
// intermediate sums and produce visible shadow flicker on PCF kernels.
//
// Detection plan: AST. Search the source bytes for `SampleCmp` calls. When
// the surrounding statement does not contain the keyword `precise`, emit.
// Conservative: a single `precise` anywhere on the same line / surrounding
// 200 bytes is enough to suppress.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "missing-precise-on-pcf";
constexpr std::string_view k_category = "bindings";
constexpr std::size_t k_window_bytes = 256U;

class MissingPreciseOnPcf : public Rule {
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
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find("SampleCmp", pos);
            if (found == std::string_view::npos)
                return;
            const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
            if (!ok_left) {
                pos = found + 1U;
                continue;
            }
            const std::size_t lo = (found > k_window_bytes) ? found - k_window_bytes : 0U;
            const std::size_t hi = std::min(found + k_window_bytes, bytes.size());
            const auto window = bytes.substr(lo, hi - lo);
            // Look for `precise` as a standalone identifier in the window.
            bool has_precise = false;
            std::size_t p = 0U;
            while (p < window.size()) {
                const auto f2 = window.find("precise", p);
                if (f2 == std::string_view::npos)
                    break;
                const bool l = (f2 == 0) || !is_id_char(window[f2 - 1]);
                const std::size_t e = f2 + std::string_view{"precise"}.size();
                const bool r = (e >= window.size()) || !is_id_char(window[e]);
                if (l && r) {
                    has_precise = true;
                    break;
                }
                p = f2 + 1U;
            }
            if (has_precise) {
                pos = found + std::string_view{"SampleCmp"}.size();
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span =
                Span{.source = tree.source_id(),
                     .bytes = ByteSpan{
                         static_cast<std::uint32_t>(found),
                         static_cast<std::uint32_t>(found + std::string_view{"SampleCmp"}.size())}};
            diag.message = std::string{
                "PCF / depth-compare site without a nearby `precise` qualifier -- fast-math "
                "reassociation can perturb the rounding of weight sums and produce visible "
                "shadow flicker"};
            ctx.emit(std::move(diag));
            pos = found + std::string_view{"SampleCmp"}.size();
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_missing_precise_on_pcf() {
    return std::make_unique<MissingPreciseOnPcf>();
}

}  // namespace hlsl_clippy::rules
