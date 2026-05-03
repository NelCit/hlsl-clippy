// coherence-hint-redundant-bits
//
// Detects a `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose
// `hintBits` argument is larger than necessary to express the actual
// coherence-hint value. Per ADR 0010 §Phase 4 (rule #6) and proposal 0027,
// the SER scheduler buckets lanes by the masked low `hintBits` bits of
// `coherenceHint`; a 16-bit declaration over a 4-bit hint dilutes the
// scheduler's grouping.
//
// Stage: ControlFlow (forward-compatible-stub for Phase 4 bit-range analysis).
//
// The full rule needs the bit-range domain that ADR 0011's
// `pack-clamp-on-prove-bounded` machinery seeds in Phase 3 plus a per-call
// constant-folder for the actual hint expression. Sub-phase 4b's CFG +
// uniformity oracle does not yet expose either signal, so this stub fires
// only on the obvious upper-bound violation: `hintBits` strictly greater
// than the spec maximum (32 in proposal 0027; NVIDIA's effective ceiling is
// 16). Anything within `(hint-actual-bits, 32]` is left silent until the
// bit-range analyzer lands. The companion blog post has the full scheduler
// rationale.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
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

constexpr std::string_view k_rule_id = "coherence-hint-redundant-bits";
constexpr std::string_view k_category = "ser";
constexpr std::string_view k_call_name = "MaybeReorderThread";

// Spec-effective ceiling: SER proposal 0027 caps `hintBits` at 32; NVIDIA's
// SER blog flags 16 as the effective scheduler width. We keep the stub
// conservative (fire above 32) to avoid false positives while the bit-range
// domain is still missing -- once the analyzer lands, the threshold tightens
// to the proven upper bound of `coherenceHint`.
constexpr std::uint32_t k_spec_ceiling = 32U;

/// Parse a small unsigned integer literal (decimal or `0x`-hex). Returns
/// `false` on any non-literal text (the bit-range analyzer will absorb those
/// once it lands).
[[nodiscard]] bool parse_uint_literal(std::string_view text, std::uint32_t& out) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == 'u' ||
                             text.back() == 'U')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    std::uint32_t value = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        for (std::size_t i = 2; i < text.size(); ++i) {
            const char c = text[i];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
            if (value > (UINT32_MAX - digit) / 16U) {
                return false;
            }
            value = value * 16U + digit;
        }
    } else {
        for (const char c : text) {
            if (c < '0' || c > '9') {
                return false;
            }
            const auto digit = static_cast<std::uint32_t>(c - '0');
            if (value > (UINT32_MAX - digit) / 10U) {
                return false;
            }
            value = value * 10U + digit;
        }
    }
    out = value;
    return true;
}

/// Walks the call's argument list and returns the third argument's node
/// (the `hintBits` parameter). Returns a null node when the call has fewer
/// than three arguments or the AST shape is unexpected.
[[nodiscard]] ::TSNode third_argument(::TSNode call) noexcept {
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
        if (named_seen == 3) {
            return child;
        }
    }
    return {};
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (!::ts_node_is_null(fn)) {
            const auto fn_text = node_text(fn, bytes);
            // Match either bare `MaybeReorderThread(` or the qualified
            // `dx::MaybeReorderThread(` form. The grammar exposes the qualified
            // name as the function field's verbatim text.
            const auto pos = fn_text.find(k_call_name);
            const bool is_target = pos != std::string_view::npos &&
                                   (pos == 0 || fn_text[pos - 1] == ':' ||
                                    fn_text[pos - 1] == '.' || fn_text[pos - 1] == ' ');
            if (is_target) {
                const ::TSNode arg = third_argument(node);
                if (!::ts_node_is_null(arg)) {
                    const auto arg_text = node_text(arg, bytes);
                    std::uint32_t bits = 0;
                    if (parse_uint_literal(arg_text, bits) && bits > k_spec_ceiling) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(arg)};
                        diag.message = std::string{
                            "`MaybeReorderThread` `hintBits` exceeds the SER spec "
                            "ceiling (32) -- the SER scheduler ignores bits above "
                            "the ceiling and the unused width dilutes lane "
                            "bucketing (proposal 0027)"};

                        // Clamp the literal to the spec ceiling. The SER scheduler
                        // masks `coherenceHint` to the low `min(hintBits, 32)` bits
                        // already, so dropping the literal from `>32` to `32` is
                        // semantics-preserving at the runtime; the only thing it
                        // changes is what the source reads as. This is the tightest
                        // bound we can prove without the bit-range domain — the
                        // doc page calls out that a tighter (e.g. 4-bit) value
                        // requires the analyzer to land. The arg is already a
                        // numeric literal (the only shape the rule fires on), so
                        // the rewrite has no side effects to repeat.
                        Fix fix;
                        fix.machine_applicable = true;
                        fix.description =
                            std::string{"clamp `hintBits` literal to the SER spec ceiling (32)"};
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = tree.byte_range(arg)};
                        edit.replacement = std::string{"32"};
                        fix.edits.push_back(std::move(edit));
                        diag.fixes.push_back(std::move(fix));

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

class CoherenceHintRedundantBits : public Rule {
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
        // Forward-compatible: the CFG / uniformity oracle has no bit-range
        // domain yet, so we walk the AST under the ControlFlow stage to keep
        // the rule on the same dispatch path as its eventual full
        // implementation. The walk fires only on the spec-ceiling violation
        // until the bit-range domain catches up.
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_coherence_hint_redundant_bits() {
    return std::make_unique<CoherenceHintRedundantBits>();
}

}  // namespace shader_clippy::rules
