// coherence-hint-encodes-shader-type
//
// Detects a `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose
// `coherenceHint` argument is data-flow-tainted by `hit.IsHit()` or
// `hit.GetShaderTableIndex()`. Per ADR 0010 §Phase 4 (rule #7) and proposal
// 0027, the SER scheduler already groups lanes by hit-group / hit-vs-miss;
// encoding the same axis in `coherenceHint` duplicates work the driver has
// already done.
//
// Stage: ControlFlow (forward-compatible-stub for Phase 4 taint analysis).
//
// The full rule needs a Phase 4 taint pass that propagates reach from
// `hit.IsHit()` / `hit.GetShaderTableIndex()` through arithmetic and
// conditional expressions. Sub-phase 4b's uniformity oracle does not yet
// expose call-result taint sinks, so this stub fires on the syntactic
// trigger: a `MaybeReorderThread` whose enclosing function contains a
// preceding read of `IsHit()` or `GetShaderTableIndex()` that appears
// textually in the second argument's expression. The taint analyzer will
// replace the textual approximation with a real flow-sensitive check.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
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

constexpr std::string_view k_rule_id = "coherence-hint-encodes-shader-type";
constexpr std::string_view k_category = "ser";
constexpr std::string_view k_call_name = "MaybeReorderThread";

// Scheduler-redundant taint sources: HitObject members the driver already
// uses to group lanes. Direct textual matching only (the Phase 4 taint
// analyzer will replace this seed list with a propagating reach-set).
constexpr std::array<std::string_view, 2> k_taint_sources{
    "GetShaderTableIndex",
    "IsHit",
};

/// Returns the second argument node (the `coherenceHint` parameter). Null on
/// any unexpected AST shape.
[[nodiscard]] ::TSNode second_argument(::TSNode call) noexcept {
    const ::TSNode args = ::ts_node_child_by_field_name(call, "arguments", 9);
    if (::ts_node_is_null(args)) {
        return {};
    }
    std::uint32_t named_seen = 0;
    const std::uint32_t count = ::ts_node_named_child_count(args);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_named_child(args, i);
        if (::ts_node_is_null(child)) {
            continue;
        }
        ++named_seen;
        if (named_seen == 2) {
            return child;
        }
    }
    return {};
}

/// Walk up to the nearest enclosing `function_definition` ancestor and return
/// its byte range, or `std::nullopt` when none exists.
[[nodiscard]] std::string_view enclosing_function_text(::TSNode start,
                                                       std::string_view bytes) noexcept {
    ::TSNode cur = start;
    while (!::ts_node_is_null(cur)) {
        if (node_kind(cur) == "function_definition") {
            return node_text(cur, bytes);
        }
        cur = ::ts_node_parent(cur);
    }
    return {};
}

/// True when `arg_text` (the coherenceHint expression) names an identifier
/// whose definition in the enclosing function reads from a taint source.
/// The textual heuristic catches both direct uses and the common indirect
/// `auto x = hit.GetShaderTableIndex(); reorder(hit, x, ...)` form.
[[nodiscard]] bool taints_via_function_text(std::string_view arg_text,
                                            std::string_view fn_text) noexcept {
    // Direct case: the argument expression itself mentions a taint source.
    for (const auto src : k_taint_sources) {
        if (arg_text.find(src) != std::string_view::npos) {
            return true;
        }
    }
    // Indirect case: the function body contains an assignment whose RHS is
    // a taint source AND whose LHS identifier appears in the argument text.
    // Cheap textual heuristic; the Phase 4 taint analyzer replaces this.
    for (const auto src : k_taint_sources) {
        std::size_t cursor = 0;
        while (cursor < fn_text.size()) {
            const auto src_pos = fn_text.find(src, cursor);
            if (src_pos == std::string_view::npos) {
                break;
            }
            cursor = src_pos + src.size();
            // Look backward for an `= ... GetShaderTableIndex(...)` pattern;
            // we then look forward to see if the LHS identifier appears in
            // arg_text.
            std::size_t eq = src_pos;
            while (eq > 0 && fn_text[eq] != '=' && fn_text[eq] != '\n' && fn_text[eq] != ';') {
                --eq;
            }
            if (eq == 0 || fn_text[eq] != '=') {
                continue;
            }
            // Walk back over whitespace.
            std::size_t end_id = eq;
            while (end_id > 0 && (fn_text[end_id - 1] == ' ' || fn_text[end_id - 1] == '\t')) {
                --end_id;
            }
            std::size_t start_id = end_id;
            while (start_id > 0) {
                const char c = fn_text[start_id - 1];
                const bool is_id_char = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                        (c >= '0' && c <= '9') || c == '_';
                if (!is_id_char) {
                    break;
                }
                --start_id;
            }
            if (start_id == end_id) {
                continue;
            }
            const auto lhs = fn_text.substr(start_id, end_id - start_id);
            if (lhs.empty()) {
                continue;
            }
            if (arg_text.find(lhs) != std::string_view::npos) {
                return true;
            }
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (!::ts_node_is_null(fn)) {
            const auto fn_text = node_text(fn, bytes);
            const auto pos = fn_text.find(k_call_name);
            const bool is_target = pos != std::string_view::npos &&
                                   (pos == 0 || fn_text[pos - 1] == ':' ||
                                    fn_text[pos - 1] == '.' || fn_text[pos - 1] == ' ');
            if (is_target) {
                const ::TSNode arg = second_argument(node);
                if (!::ts_node_is_null(arg)) {
                    const auto arg_text = node_text(arg, bytes);
                    const auto enclosing = enclosing_function_text(node, bytes);
                    if (taints_via_function_text(arg_text, enclosing)) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(arg)};
                        diag.message = std::string{
                            "`MaybeReorderThread` coherence hint encodes hit-group "
                            "or hit-vs-miss state -- the SER scheduler already "
                            "buckets on this axis; pick an application-specific "
                            "axis (material id, BVH instance) instead "
                            "(proposal 0027)"};
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoherenceHintEncodesShaderType : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& /*cfg*/, RuleContext& ctx) override {
        // Forward-compatible: stays on the ControlFlow dispatch path so the
        // Phase 4 taint analyzer can replace the textual approximation
        // without moving the rule between stages.
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_coherence_hint_encodes_shader_type() {
    return std::make_unique<CoherenceHintEncodesShaderType>();
}

}  // namespace hlsl_clippy::rules
