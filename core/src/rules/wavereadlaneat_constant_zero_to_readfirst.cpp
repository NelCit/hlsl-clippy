// wavereadlaneat-constant-zero-to-readfirst
//
// Detects `WaveReadLaneAt(x, 0)` and rewrites to `WaveReadLaneFirst(x)`. The
// two intrinsics are functionally equivalent when the lane index is the
// known-active first lane (lane 0 is always active in a non-empty wave), but
// `WaveReadLaneFirst` lets the compiler skip the lane-index broadcast on
// RDNA / Ada -- it lowers to a single subgroup-broadcast-first opcode rather
// than a generic per-lane broadcast.
//
// Detection (purely AST):
//   call_expression to `WaveReadLaneAt` with exactly 2 arguments, where the
//   second argument is a number_literal whose value is exactly 0.
//
// The fix is machine-applicable: textually equivalent at every IHV.

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

constexpr std::string_view k_rule_id = "wavereadlaneat-constant-zero-to-readfirst";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_intrinsic_name = "WaveReadLaneAt";

[[nodiscard]] bool is_int_suffix(char c) noexcept {
    return c == 'u' || c == 'U' || c == 'l' || c == 'L';
}

/// True if `text` is exactly the literal 0 (with optional leading zeros, no
/// fraction with non-zero digit, optional integer suffix `u`/`U`/`l`/`L`).
[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i < text.size() && text[i] >= '1' && text[i] <= '9')
        return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_int_suffix(text[i]) && text[i] != 'f' && text[i] != 'F' && text[i] != 'h' &&
            text[i] != 'H') {
            return false;
        }
        ++i;
    }
    return true;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_intrinsic_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
                const ::TSNode x = ::ts_node_named_child(args, 0);
                const ::TSNode lane = ::ts_node_named_child(args, 1);
                if (node_kind(lane) == "number_literal" &&
                    literal_is_zero(node_text(lane, bytes))) {
                    const auto x_text = node_text(x, bytes);
                    if (!x_text.empty()) {
                        const auto call_range = tree.byte_range(node);
                        const std::string replacement =
                            std::string{"WaveReadLaneFirst("} + std::string{x_text} + ")";

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                        diag.message = std::string{
                            "`WaveReadLaneAt(x, 0)` is equivalent to "
                            "`WaveReadLaneFirst(x)` -- the latter lets the "
                            "compiler emit a subgroup-broadcast-first opcode "
                            "instead of a generic per-lane broadcast on RDNA / Ada"};

                        Fix fix;
                        fix.machine_applicable = true;
                        fix.description = std::string{"replace with `WaveReadLaneFirst(x)`"};
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                        edit.replacement = replacement;
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

class WaveReadLaneAtConstantZeroToReadFirst : public Rule {
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

std::unique_ptr<Rule> make_wavereadlaneat_constant_zero_to_readfirst() {
    return std::make_unique<WaveReadLaneAtConstantZeroToReadFirst>();
}

}  // namespace shader_clippy::rules
