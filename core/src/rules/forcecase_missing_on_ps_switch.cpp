// forcecase-missing-on-ps-switch
//
// Detects a `switch` statement inside a pixel-shader entry point where at
// least one case body contains a derivative-bearing operation (`Sample`,
// `SampleBias`, `SampleGrad`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`) and the
// `switch` lacks the `[forcecase]` attribute. Per ADR 0011 §Phase 4 (rule
// pack C) and the rule's doc page.
//
// Stage: `Ast`. The rule walks the AST looking for `switch_statement` nodes
// whose enclosing function is a PS entry point. For each such switch, it
// inspects the switch body for derivative-bearing intrinsic calls and the
// preceding bytes for the `[forcecase]` attribute. The rule fires only on
// PS entry points -- the hazard is specific to the quad / derivative model.
//
// Detection (purely AST):
//   1. Walk every `function_definition`. Mark functions whose attribute
//      list carries `[shader("pixel")]` OR whose return-or-parameter
//      semantics include `: SV_Target*` (a strong PS-stage tell).
//   2. Within each PS function, walk for `switch_statement` nodes.
//   3. For each switch, inspect the bytes immediately preceding the
//      `switch` keyword. If `[forcecase]` is present, skip.
//   4. Scan the switch body text for any derivative-bearing intrinsic.
//      If at least one is present, emit on the switch statement.
//
// Conservatism contract: the PS-stage detection is purely textual (the
// `[shader("pixel")]` attribute or `: SV_Target*` semantic). If the entry
// point is declared via a separate root-signature / pipeline state that
// the source does not name, the rule will not fire. The fix is
// `suggestion`-only: adding `[forcecase]` is a one-token change but the
// developer may have a reason for the chained-if lowering.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "forcecase-missing-on-ps-switch";
constexpr std::string_view k_category = "control-flow";
constexpr std::string_view k_forcecase_attr = "[forcecase]";
constexpr std::string_view k_pixel_tag = "\"pixel\"";

constexpr std::array<std::string_view, 7> k_derivative_calls{
    "Sample",
    "SampleBias",
    "SampleGrad",
    "ddx",
    "ddy",
    "ddx_fine",
    "ddy_fine",
};

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_token(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]) ||
                              text[end] == '(' || text[end] == '.';
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// True when `text` looks like a pixel-shader entry-point function. We
/// accept either `[shader("pixel")]` (the preferred annotation) or the
/// presence of a `SV_Target` semantic in the parameter / return list.
[[nodiscard]] bool is_pixel_function(std::string_view fn_text) noexcept {
    if (fn_text.find(k_pixel_tag) != std::string_view::npos &&
        fn_text.find("shader") != std::string_view::npos) {
        return true;
    }
    return fn_text.find("SV_Target") != std::string_view::npos;
}

/// True when the bytes immediately preceding `switch_lo` carry the
/// `[forcecase]` attribute. Walks backward over whitespace and looks for
/// the literal `[forcecase]` suffix.
[[nodiscard]] bool preceded_by_forcecase(std::string_view bytes, std::uint32_t switch_lo) noexcept {
    if (switch_lo == 0U || switch_lo > bytes.size()) {
        return false;
    }
    std::uint32_t i = switch_lo;
    while (i > 0U) {
        const char c = bytes[i - 1U];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            --i;
            continue;
        }
        break;
    }
    if (i < k_forcecase_attr.size()) {
        return false;
    }
    return bytes.substr(i - k_forcecase_attr.size(), k_forcecase_attr.size()) == k_forcecase_attr;
}

/// True when the switch body text contains at least one derivative-bearing
/// intrinsic call.
[[nodiscard]] bool body_uses_derivatives(std::string_view text) noexcept {
    for (const auto call : k_derivative_calls) {
        if (has_token(text, call)) {
            return true;
        }
    }
    return false;
}

void scan_switches(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "switch_statement") {
        const auto switch_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
        if (!preceded_by_forcecase(bytes, switch_lo)) {
            const auto switch_text = node_text(node, bytes);
            if (body_uses_derivatives(switch_text)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "PS `switch` containing derivative-bearing intrinsic (Sample / ddx / ddy / "
                    "...) is missing `[forcecase]` -- without the hint the compiler may emit a "
                    "chained-if ladder, retiring quad lanes one arm at a time and feeding "
                    "undefined derivatives to surviving lanes; add `[forcecase]` to pin the "
                    "lowering to a jump-table dispatch and preserve quad coherence"};
                ctx.emit(std::move(diag));
                return;  // do not descend into this switch's nested scopes.
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_switches(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

void walk_functions(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        if (is_pixel_function(fn_text)) {
            scan_switches(node, bytes, tree, ctx);
        }
        return;  // do not descend into other functions inside a function.
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk_functions(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ForceCaseMissingOnPsSwitch : public Rule {
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
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        walk_functions(root, tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_forcecase_missing_on_ps_switch() {
    return std::make_unique<ForceCaseMissingOnPsSwitch>();
}

}  // namespace shader_clippy::rules
