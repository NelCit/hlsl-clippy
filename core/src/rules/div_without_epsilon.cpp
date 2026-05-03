// div-without-epsilon
//
// Detects `x / length(...)` and `x / dot(...)` expressions where the divisor
// can hit zero (length of a possibly-zero vector / dot of orthogonal vectors)
// without an epsilon guard such as `max(epsilon, ...)`.
//
// Stage: Ast. The detection is textual on `binary_expression` nodes whose
// operator is `/`.
//
// Fix grade (v1.2 -- ADR 0019, configurable epsilon surface + purity oracle):
//   * Rewrites `<num> / <divisor>` to `<num> / max(<eps>, <divisor>)`, where
//     `<eps>` is taken from `Config::div_epsilon()` (default 1e-6) when the
//     rule has access to a `Config`, falling back to the literal `1e-6f`
//     otherwise.
//   * Machine-applicable when the divisor classifies as `SideEffectFree`
//     under the purity oracle. The textual rewrite preserves operand
//     evaluation count (the divisor still evaluates exactly once), so
//     purity is sufficient.
//   * Suggestion-only when the divisor is impure or unclassified.

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/purity_oracle.hpp"
#include "shader_clippy/config.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "div-without-epsilon";
constexpr std::string_view k_category = "math";

/// Render a float as a HLSL-grammar-valid literal with `f` suffix. We aim
/// for a spelling the user will recognise in their `.shader-clippy.toml`:
///   * the default `div_epsilon` (1e-6) renders as `1e-06f`;
///   * the default `compare_epsilon` (1e-4) renders as `0.0001f` (handled
///     by `compare_equal_float` -- exposed here for symmetry);
///   * other values fall through to a precision-7 rendering (one short of
///     `numeric_limits<float>::max_digits10` to avoid trailing-noise
///     digits like `9.9999997e-05f` when the user spelled the value
///     `0.0001f`).
[[nodiscard]] std::string render_float_literal(float value) {
    if (value == k_default_compare_epsilon) {
        return std::string{"0.0001f"};
    }
    if (value == k_default_div_epsilon) {
        return std::string{"1e-06f"};
    }
    std::ostringstream os;
    os.precision(7);
    os << value;
    std::string out = os.str();
    if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
        out.find('E') == std::string::npos) {
        out += ".0";
    }
    out += 'f';
    return out;
}

[[nodiscard]] bool divisor_is_unguarded_length_or_dot(std::string_view rhs) noexcept {
    // Trim whitespace and leading parentheses.
    while (!rhs.empty() && (rhs.front() == ' ' || rhs.front() == '\t' || rhs.front() == '(')) {
        rhs.remove_prefix(1);
    }
    // Reject obvious guards.
    if (rhs.starts_with("max(") || rhs.starts_with("max ("))
        return false;
    // Match `length(` / `dot(` at the start.
    return rhs.starts_with("length(") || rhs.starts_with("length (") || rhs.starts_with("dot(") ||
           rhs.starts_with("dot (");
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression") {
        const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
        if (node_text(op, bytes) == "/") {
            const auto right = ::ts_node_child_by_field_name(node, "right", 5);
            const auto rhs_text = node_text(right, bytes);
            if (divisor_is_unguarded_length_or_dot(rhs_text)) {
                // v1.2 (ADR 0019): pick the project-tuned epsilon when a
                // Config is in scope; otherwise fall back to the documented
                // hard-coded default. The rewrite is machine-applicable iff
                // the divisor classifies as pure under the side-effect
                // oracle (the rewrite preserves divisor evaluation count;
                // `max(eps, x)` evaluates `x` once, same as the original).
                const float epsilon =
                    (ctx.config() != nullptr) ? ctx.config()->div_epsilon() : k_default_div_epsilon;
                const std::string eps_literal = render_float_literal(epsilon);

                const auto divisor_purity = util::classify_expression(tree, right);
                const bool divisor_pure = divisor_purity == util::Purity::SideEffectFree;

                const auto right_range = tree.byte_range(right);
                const std::string replacement =
                    std::string{"max("} + eps_literal + ", " + std::string{rhs_text} + ")";

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "division by length(...) / dot(...) without epsilon guard -- "
                    "produces +Inf or NaN at zero divisors; wrap the divisor in "
                    "max(epsilon, ...) or use the SafeNormalize idiom"};

                Fix fix;
                fix.machine_applicable = divisor_pure;
                fix.description =
                    std::string{"wrap the divisor in `max("} + eps_literal + ", ...)` (epsilon " +
                    ((ctx.config() != nullptr) ? "from `[float] div-epsilon`" : "= default 1e-6") +
                    "); Inf / NaN propagation through subsequent ops corrupts "
                    "neighbouring pixels in TAA / denoiser pipelines" +
                    (divisor_pure
                         ? std::string{}
                         : std::string{
                               " -- suggestion-only: the divisor is not side-effect-free under "
                               "the purity oracle, hand-review the rewrite before applying"});
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = right_range};
                edit.replacement = replacement;
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class DivWithoutEpsilon : public Rule {
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

std::unique_ptr<Rule> make_div_without_epsilon() {
    return std::make_unique<DivWithoutEpsilon>();
}

}  // namespace shader_clippy::rules
