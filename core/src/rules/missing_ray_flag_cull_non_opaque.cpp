// missing-ray-flag-cull-non-opaque
//
// Detects `TraceRay(...)` / `RayQuery::TraceRayInline(...)` calls whose
// ray-flag argument does not include `RAY_FLAG_CULL_NON_OPAQUE` and that
// share a translation unit where there is no any-hit shader (i.e. no
// `[shader("anyhit")]` attribute). Without the flag the BVH walker bounces
// every potentially-non-opaque leaf back to SIMT for an empty any-hit.
//
// Detection plan: AST. Look for the literal `TraceRay` and `TraceRayInline`
// call shapes; pick out the ray-flag argument (the second argument for
// `TraceRay`, the first for `TraceRayInline`); confirm it is a constant
// expression of zero-or-more `RAY_FLAG_*` bit-or'ed values; emit when
// `RAY_FLAG_CULL_NON_OPAQUE` is missing AND the file has no anyhit attr.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "missing-ray-flag-cull-non-opaque";
constexpr std::string_view k_category = "dxr";

void scan_call(std::string_view bytes,
               std::string_view fn_name,
               std::size_t flag_arg_index,
               bool has_anyhit,
               const AstTree& tree,
               RuleContext& ctx) {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(fn_name, pos);
        if (found == std::string_view::npos)
            return;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + fn_name.size();
        const bool ok_right = (end < bytes.size() && bytes[end] == '(');
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        // Find matching `)`.
        int depth = 0;
        std::size_t i = end;
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
            pos = end;
            continue;
        }
        const auto args = bytes.substr(end + 1U, i - end - 1U);
        // Split at top-level commas.
        int d = 0;
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        std::size_t start = 0U;
        for (std::size_t k = 0; k <= args.size(); ++k) {
            if (k == args.size() || (args[k] == ',' && d == 0)) {
                spans.emplace_back(start, k);
                start = k + 1U;
            } else if (args[k] == '(') {
                ++d;
            } else if (args[k] == ')') {
                --d;
            }
        }
        if (flag_arg_index >= spans.size()) {
            pos = i + 1U;
            continue;
        }
        const auto flag_arg =
            args.substr(spans[flag_arg_index].first,
                        spans[flag_arg_index].second - spans[flag_arg_index].first);
        // Flag-arg should mention a RAY_FLAG_* token. If it doesn't, we
        // bail (could be a runtime-computed value).
        if (flag_arg.find("RAY_FLAG_") == std::string_view::npos) {
            pos = i + 1U;
            continue;
        }
        if (flag_arg.find("RAY_FLAG_CULL_NON_OPAQUE") != std::string_view::npos) {
            pos = i + 1U;
            continue;
        }
        if (has_anyhit) {
            // The shader genuinely uses anyhit; do not push the user toward
            // dropping the bounce.
            pos = i + 1U;
            continue;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(),
                                 .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                   static_cast<std::uint32_t>(i + 1U)}};
        diag.message = std::string{"`"} + std::string{fn_name} +
                       "` ray-flag argument omits `RAY_FLAG_CULL_NON_OPAQUE` -- with no "
                       "`[shader(\"anyhit\")]` in this translation unit, every potentially-non-"
                       "opaque BVH leaf bounces back to SIMT for an empty any-hit invocation";
        ctx.emit(std::move(diag));
        pos = i + 1U;
    }
}

class MissingRayFlagCullNonOpaque : public Rule {
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
        const bool has_anyhit = bytes.find("\"anyhit\"") != std::string_view::npos;
        scan_call(bytes, "TraceRay", 1U, has_anyhit, tree, ctx);
        scan_call(bytes, "TraceRayInline", 0U, has_anyhit, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_missing_ray_flag_cull_non_opaque() {
    return std::make_unique<MissingRayFlagCullNonOpaque>();
}

}  // namespace hlsl_clippy::rules
