// precise-missing-on-iterative-refine
//
// Detects a `for` / `while` / `do` loop body whose statements iteratively
// refine a floating-point value (Newton-Raphson, Halley, Kahan summation,
// rsqrt refinement) where the iteration variable lacks the `precise`
// qualifier. Without `precise`, fast-math reordering may collapse the
// iteration to the initial guess.
//
// Stage: Ast.
//
// Detection plan: walk every `for_statement` / `while_statement` /
// `do_statement` and inspect the body. If the body assigns to the same
// variable name on both sides of the assignment (the canonical iterative-
// refine shape `x = x op ...`) AND the variable's declaration in the same
// source does not carry `precise`, emit on the variable's first such
// self-assignment span. We require the assignment to be a *self-update*
// (`x = ... x ...`) rather than any in-loop write to avoid firing on plain
// accumulator loops the optimiser is already free to vectorise. Bail when
// the loop body contains explicit `precise` declarations.
//
// Forward-compatible stub: the doc page also wants a richer "iteration
// shape" detector (e.g. recognising the rsqrt-refine identity
// `x = x * (1.5 - 0.5 * a * x * x)`) and an `iteration-threshold` config
// option. The shape we ship is the canonical `x = f(x)` self-update inside
// a loop, which matches every doc-page Bad example. The richer shapes
// follow when light-dataflow tracking lands per ADR 0013.

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

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "precise-missing-on-iterative-refine";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_precise = "precise";

[[nodiscard]] std::size_t find_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0U;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return std::string_view::npos;
        }
        const bool ok_left = (found == 0U) || !is_id_char(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || !is_id_char(text[end]);
        if (ok_left && ok_right) {
            return found;
        }
        pos = found + 1U;
    }
    return std::string_view::npos;
}

[[nodiscard]] bool body_has_precise_declarator(std::string_view text) noexcept {
    return find_keyword(text, k_precise) != std::string_view::npos;
}

/// True when the loop body contains a statement of the form
/// `<id> = ...<id>...;` where the identifier on both sides is the same
/// non-trivial token. Returns the identifier name and the byte offset of
/// the first qualifying assignment relative to `body_text` via outparams.
[[nodiscard]] bool find_self_update(std::string_view body_text,
                                    std::string_view& var_name,
                                    std::size_t& assign_offset) noexcept {
    std::size_t i = 0U;
    while (i < body_text.size()) {
        // Find an identifier start.
        if (!is_id_char(body_text[i]) || (i > 0U && is_id_char(body_text[i - 1U]))) {
            ++i;
            continue;
        }
        const std::size_t name_start = i;
        while (i < body_text.size() && is_id_char(body_text[i])) {
            ++i;
        }
        const std::size_t name_end = i;
        const auto name = body_text.substr(name_start, name_end - name_start);
        if (name.empty() || name == "for" || name == "while" || name == "if" || name == "else" ||
            name == "do" || name == "return") {
            continue;
        }
        // Skip whitespace.
        std::size_t k = name_end;
        while (k < body_text.size() && (body_text[k] == ' ' || body_text[k] == '\t')) {
            ++k;
        }
        if (k >= body_text.size() || body_text[k] != '=' ||
            (k + 1U < body_text.size() && body_text[k + 1U] == '=')) {
            continue;
        }
        // Look for the same identifier on the RHS up to the next `;`.
        const std::size_t stmt_end = body_text.find(';', k);
        if (stmt_end == std::string_view::npos) {
            return false;
        }
        const auto rhs = body_text.substr(k + 1U, stmt_end - (k + 1U));
        const auto rhs_self = find_keyword(rhs, name);
        if (rhs_self != std::string_view::npos) {
            var_name = name;
            assign_offset = name_start;
            return true;
        }
        i = stmt_end + 1U;
    }
    return false;
}

/// True when the source declares `<var_name>` as `precise` somewhere outside
/// the loop body itself. We accept any declaration of the form
/// `precise <type> <var_name>` token-adjacent in the source.
[[nodiscard]] bool variable_declared_precise(std::string_view bytes,
                                             std::string_view var_name) noexcept {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto p = bytes.find(k_precise, pos);
        if (p == std::string_view::npos) {
            return false;
        }
        const std::size_t end = p + k_precise.size();
        const bool ok_left = (p == 0U) || !is_id_char(bytes[p - 1U]);
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = p + 1U;
            continue;
        }
        // After `precise`, expect optional whitespace + type + whitespace +
        // the variable name. We approximate by scanning for the variable
        // name within the next ~64 bytes.
        const std::size_t window_end = std::min(bytes.size(), end + 64U);
        const auto window = bytes.substr(end, window_end - end);
        if (find_keyword(window, var_name) != std::string_view::npos) {
            return true;
        }
        pos = end;
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool is_loop =
        (kind == "for_statement" || kind == "while_statement" || kind == "do_statement");
    if (is_loop) {
        const auto loop_text = node_text(node, bytes);
        if (!body_has_precise_declarator(loop_text)) {
            std::string_view var_name;
            std::size_t local_offset = 0U;
            if (find_self_update(loop_text, var_name, local_offset)) {
                if (!variable_declared_precise(bytes, var_name)) {
                    const auto loop_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                    const std::uint32_t var_lo = loop_lo + static_cast<std::uint32_t>(local_offset);
                    const std::uint32_t var_hi =
                        var_lo + static_cast<std::uint32_t>(var_name.size());
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{.source = tree.source_id(),
                                             .bytes = ByteSpan{.lo = var_lo, .hi = var_hi}};
                    diag.message =
                        std::string{"iteration variable `"} + std::string{var_name} +
                        "` is updated as `x = f(x)` inside a loop without `precise` -- "
                        "fast-math reordering may collapse the refinement to the initial "
                        "guess; declare the variable as `precise` to protect the iteration";
                    ctx.emit(std::move(diag));
                }
            }
        }
        return;  // do not descend into nested loops here; the outer walk
                 // continues at the loop's siblings.
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class PreciseMissingOnIterativeRefine : public Rule {
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

std::unique_ptr<Rule> make_precise_missing_on_iterative_refine() {
    return std::make_unique<PreciseMissingOnIterativeRefine>();
}

}  // namespace shader_clippy::rules
