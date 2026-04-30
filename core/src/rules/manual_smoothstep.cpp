// manual-smoothstep
//
// Detects the hand-rolled cubic Hermite interpolation pattern that is
// equivalent to HLSL's `smoothstep(a, b, t)` intrinsic:
//
//     float n = saturate((t - a) / (b - a));
//     return n * n * (3.0 - 2.0 * n);
//
// The match looks for two consecutive statements in the same compound_statement
// where:
//   1. A local variable `n` is assigned `saturate((t - a) / (b - a))`.
//   2. The next effective statement evaluates (or returns) `n * n * (3 - 2 * n)`.
//
// Matching strategy:
//   - Walk every compound_statement.
//   - For each pair of consecutive child statements, try to match the pattern.
//   - "saturate(...)" is detected by checking for a call to "saturate" with a
//     single argument that is a binary_expression `/`.
//   - The second statement pattern `n * n * (3 - 2*n)` is checked textually
//     after normalising whitespace — we match `n*n*(3` / `n * n * (3` variants.
//
// The fix is SUGGESTION-ONLY because the two statements cross statement
// boundaries and the surrounding code may reuse variable `n`.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "manual-smoothstep";
constexpr std::string_view k_category = "math";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

/// Returns the anonymous operator text of a binary_expression.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child)) continue;
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
        if (lo < bytes.size() && hi <= bytes.size() && hi > lo)
            return bytes.substr(lo, hi - lo);
    }
    return {};
}

/// True if `node` is a call to `saturate` with a single argument.
[[nodiscard]] bool is_saturate_call(::TSNode node, std::string_view bytes) noexcept {
    if (node_kind(node) != "call_expression") return false;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (node_text(fn, bytes) != "saturate") return false;
    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    return !::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 1U;
}

/// Strip whitespace from `s` for textual comparison purposes.
[[nodiscard]] std::string strip_ws(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') result += c;
    }
    return result;
}

struct SmoothstepParams {
    std::string n_var;  ///< Name of the intermediate variable.
    std::string a_text;
    std::string b_text;
    std::string t_text;
};

/// Try to extract the saturate initialiser from a local-variable declaration
/// of the form `float n = saturate((t - a) / (b - a));`.
///
/// Returns the extracted params if matched, nullopt otherwise.
[[nodiscard]] std::optional<SmoothstepParams>
try_match_saturate_decl(::TSNode stmt, std::string_view bytes) noexcept {
    // Statement must be a declaration
    if (node_kind(stmt) != "declaration") return std::nullopt;

    // Find the declarator — it should be an init_declarator.
    // Walk named children to find the init_declarator.
    ::TSNode init_decl{};
    const uint32_t nc = ::ts_node_named_child_count(stmt);
    for (uint32_t i = 0; i < nc; ++i) {
        const ::TSNode child = ::ts_node_named_child(stmt, i);
        if (node_kind(child) == "init_declarator") {
            init_decl = child;
            break;
        }
    }
    if (::ts_node_is_null(init_decl)) return std::nullopt;

    // Declarator name
    const ::TSNode decl_name =
        ::ts_node_child_by_field_name(init_decl, "declarator", 10);
    if (node_kind(decl_name) != "identifier") return std::nullopt;
    const auto n_var = std::string{node_text(decl_name, bytes)};

    // Initializer value
    const ::TSNode init_value =
        ::ts_node_child_by_field_name(init_decl, "value", 5);
    if (!is_saturate_call(init_value, bytes)) return std::nullopt;

    // The single argument to saturate should be `(t - a) / (b - a)`.
    const ::TSNode sat_args =
        ::ts_node_child_by_field_name(init_value, "arguments", 9);
    const ::TSNode sat_arg = ::ts_node_named_child(sat_args, 0);
    if (node_kind(sat_arg) != "binary_expression") return std::nullopt;
    if (binary_op(sat_arg, bytes) != "/") return std::nullopt;

    const ::TSNode num_node =
        ::ts_node_child_by_field_name(sat_arg, "left", 4);
    const ::TSNode den_node =
        ::ts_node_child_by_field_name(sat_arg, "right", 5);

    // numerator: (t - a)  — may be parenthesized
    ::TSNode num = num_node;
    if (node_kind(num) == "parenthesized_expression") {
        num = ::ts_node_named_child(num, 0);
    }
    if (node_kind(num) != "binary_expression") return std::nullopt;
    if (binary_op(num, bytes) != "-") return std::nullopt;

    const auto t_text = std::string{node_text(
        ::ts_node_child_by_field_name(num, "left", 4), bytes)};
    const auto a_text = std::string{node_text(
        ::ts_node_child_by_field_name(num, "right", 5), bytes)};
    if (t_text.empty() || a_text.empty()) return std::nullopt;

    // denominator: (b - a)  — may be parenthesized
    ::TSNode den = den_node;
    if (node_kind(den) == "parenthesized_expression") {
        den = ::ts_node_named_child(den, 0);
    }
    if (node_kind(den) != "binary_expression") return std::nullopt;
    if (binary_op(den, bytes) != "-") return std::nullopt;

    const auto b_text = std::string{node_text(
        ::ts_node_child_by_field_name(den, "left", 4), bytes)};
    const auto a2_text = std::string{node_text(
        ::ts_node_child_by_field_name(den, "right", 5), bytes)};
    if (b_text.empty() || a2_text.empty()) return std::nullopt;

    // Both 'a' sides must match.
    if (a_text != a2_text) return std::nullopt;

    return SmoothstepParams{n_var, a_text, b_text, t_text};
}

/// True if `stripped` (whitespace-removed text of a statement) matches the
/// cubic Hermite form `n*n*(3-2*n)` or `n*n*(3.0-2.0*n)` etc.
/// We accept both `return n*n*(...)` and plain expression statements.
[[nodiscard]] bool matches_hermite_form(const std::string& stripped,
                                        const std::string& n_var) noexcept {
    // Build the expected pattern: n*n*(3 - 2*n)  (stripped of whitespace).
    // We accept literals 3/3.0/3.0f and 2/2.0/2.0f.
    // Strategy: look for the pattern n*n*(3 and end with *n) or n*n*(3.0...
    // We match by looking for key substrings in order.
    const std::string nn = n_var + "*" + n_var + "*";

    // Find n*n*
    const auto nn_pos = stripped.find(nn);
    if (nn_pos == std::string::npos) return false;

    // After nn*, expect '(' or '(3'
    const auto after_nn = nn_pos + nn.size();
    if (after_nn >= stripped.size()) return false;

    // Find '(' that follows nn*
    if (stripped[after_nn] != '(') return false;
    const auto paren_pos = after_nn;

    // After '(' we expect 3 or 3. optionally followed by digits/f/h
    const auto after_paren = paren_pos + 1;
    if (after_paren >= stripped.size() || stripped[after_paren] != '3') return false;

    // Find '*' then n_var then ')' to close the inner paren.
    // Look for  -2*n  somewhere inside the parens.
    const std::string two_n = "2*" + n_var;
    const std::string two_n2 = "2.0*" + n_var;
    const bool has_pattern =
        stripped.find(two_n, after_paren) != std::string::npos ||
        stripped.find(two_n2, after_paren) != std::string::npos;
    if (!has_pattern) return false;

    // The whole thing should end with )*n) or just )
    // At minimum: the trailing n_var that closes the inner expression.
    return true;
}

/// Collect named statement children of a compound_statement into a vector.
[[nodiscard]] std::vector<::TSNode> get_statements(::TSNode compound) {
    std::vector<::TSNode> stmts;
    const uint32_t count = ::ts_node_child_count(compound);
    for (uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(compound, i);
        if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
            const auto k = node_kind(child);
            // Skip comment nodes.
            if (k != "comment") stmts.push_back(child);
        }
    }
    return stmts;
}

void walk_compound_statements(::TSNode node,
                              std::string_view bytes,
                              const AstTree& tree,
                              RuleContext& ctx) {
    if (::ts_node_is_null(node)) return;

    if (node_kind(node) == "compound_statement") {
        const auto stmts = get_statements(node);
        for (std::size_t i = 0; i + 1 < stmts.size(); ++i) {
            const auto params = try_match_saturate_decl(stmts[i], bytes);
            if (!params) continue;

            // Check the next statement for the Hermite form.
            const auto next_text = std::string{node_text(stmts[i + 1], bytes)};
            const auto stripped = strip_ws(next_text);

            if (!matches_hermite_form(stripped, params->n_var)) continue;

            // Match! Fire on the pair of statements — span from start of decl
            // to end of second statement.
            const auto lo = static_cast<std::uint32_t>(
                ::ts_node_start_byte(stmts[i]));
            const auto hi = static_cast<std::uint32_t>(
                ::ts_node_end_byte(stmts[i + 1]));
            const ByteSpan span_range{lo, hi};

            Diagnostic diag;
            diag.code     = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = span_range};
            diag.message =
                std::string{"hand-rolled cubic Hermite interpolation — "
                            "`"} + params->n_var + " = saturate(("
                + params->t_text + " - " + params->a_text + ") / ("
                + params->b_text + " - " + params->a_text + ")); "
                + params->n_var + " * " + params->n_var + " * (3 - 2 * "
                + params->n_var + ")` is `smoothstep("
                + params->a_text + ", " + params->b_text + ", "
                + params->t_text + ")`";

            Fix fix;
            fix.machine_applicable = false;
            fix.description =
                std::string{"replace both statements with `smoothstep("} +
                params->a_text + ", " + params->b_text + ", " +
                params->t_text + ")` — check that `" + params->n_var +
                "` is not used elsewhere in the function before applying";
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
            // Skip i+1 so we don't double-fire.
            ++i;
        }
    }

    // Recurse into children.
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk_compound_statements(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualSmoothstep : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return k_rule_id; }
    [[nodiscard]] std::string_view category() const noexcept override { return k_category; }
    [[nodiscard]] Stage stage() const noexcept override { return Stage::Ast; }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        walk_compound_statements(
            ::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_smoothstep() {
    return std::make_unique<ManualSmoothstep>();
}

}  // namespace hlsl_clippy::rules
