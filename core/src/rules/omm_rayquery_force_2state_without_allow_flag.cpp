// omm-rayquery-force-2state-without-allow-flag
//
// Detects a `RayQuery<RAY_FLAG_FORCE_OMM_2_STATE>` template instantiation
// without `RAY_FLAG_ALLOW_OPACITY_MICROMAPS` ORed alongside. The DXR 1.2 OMM
// specification requires both flags to coexist; without the allow flag the
// hardware never consults the OMM blocks, so the force-2-state instruction
// has nothing to act on.
//
// Stage: Ast (forward-compatible-stub).
//
// Detection scans declaration-like nodes for the template-argument shape;
// constant-folded forms (e.g. flag bundle in a `static const uint`) wait for
// Phase 4 value tracking.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "omm-rayquery-force-2state-without-allow-flag";
constexpr std::string_view k_category = "opacity-micromaps";

constexpr std::string_view k_force = "RAY_FLAG_FORCE_OMM_2_STATE";
constexpr std::string_view k_allow = "RAY_FLAG_ALLOW_OPACITY_MICROMAPS";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    if (kind == "declaration" || kind == "global_variable_declaration" ||
        kind == "field_declaration") {
        const auto text = node_text(node, bytes);
        const auto rq_pos = text.find("RayQuery<");
        if (rq_pos != std::string_view::npos) {
            // Restrict the substring to the template arg list.
            const auto end = text.find('>', rq_pos);
            if (end != std::string_view::npos) {
                const auto args = text.substr(rq_pos, end - rq_pos);
                const bool has_force = args.find(k_force) != std::string_view::npos;
                const bool has_allow = args.find(k_allow) != std::string_view::npos;
                if (has_force && !has_allow) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Error;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "`RAY_FLAG_FORCE_OMM_2_STATE` requires "
                        "`RAY_FLAG_ALLOW_OPACITY_MICROMAPS` to be set on the "
                        "same RayQuery; without the allow flag the OMM "
                        "blocks are never consulted (DXR 1.2 OMM spec)"};

                    // OR the missing allow flag into the existing template-arg
                    // expression. The flag list is the substring between
                    // `RayQuery<` and the matching `>`; we replace it in place
                    // with `<existing> | RAY_FLAG_ALLOW_OPACITY_MICROMAPS`. The
                    // existing flag expression is not duplicated so the rewrite
                    // has no side-effect concerns. Marked
                    // `machine_applicable = false` per the doc page: adding the
                    // allow flag changes the trace's semantics and the
                    // developer must confirm the BVH has OMM blocks attached.
                    const auto node_lo =
                        static_cast<std::uint32_t>(::ts_node_start_byte(node));
                    const std::uint32_t arg_lo = node_lo +
                        static_cast<std::uint32_t>(rq_pos +
                                                   std::string_view{"RayQuery<"}.size());
                    const std::uint32_t arg_hi =
                        node_lo + static_cast<std::uint32_t>(end);
                    if (arg_lo < arg_hi) {
                        // Trim trailing whitespace so the inserted ` | ...`
                        // lands flush against the last non-space character.
                        std::uint32_t trim_hi = arg_hi;
                        while (trim_hi > arg_lo) {
                            const char c = bytes[trim_hi - 1U];
                            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                                --trim_hi;
                            } else {
                                break;
                            }
                        }
                        std::uint32_t trim_lo = arg_lo;
                        while (trim_lo < trim_hi) {
                            const char c = bytes[trim_lo];
                            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                                ++trim_lo;
                            } else {
                                break;
                            }
                        }
                        if (trim_lo < trim_hi) {
                            const auto flag_text =
                                bytes.substr(trim_lo, trim_hi - trim_lo);
                            Fix fix;
                            fix.machine_applicable = false;
                            fix.description = std::string{
                                "OR `RAY_FLAG_ALLOW_OPACITY_MICROMAPS` into the "
                                "RayQuery template-flag argument; verify the "
                                "BVH has OMM blocks attached"};
                            TextEdit edit;
                            edit.span = Span{
                                .source = tree.source_id(),
                                .bytes = ByteSpan{trim_lo, trim_hi},
                            };
                            std::string replacement;
                            replacement.reserve(flag_text.size() + 36U);
                            replacement.append(flag_text);
                            replacement.append(" | RAY_FLAG_ALLOW_OPACITY_MICROMAPS");
                            edit.replacement = std::move(replacement);
                            fix.edits.push_back(std::move(edit));
                            diag.fixes.push_back(std::move(fix));
                        }
                    }

                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class OmmRayQueryForce2StateWithoutAllowFlag : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_omm_rayquery_force_2state_without_allow_flag() {
    return std::make_unique<OmmRayQueryForce2StateWithoutAllowFlag>();
}

}  // namespace hlsl_clippy::rules
