// pow-integer-decomposition
//
// Detects `pow(x, N.0)` for integer N >= 5 and suggests pow-by-squaring.
// For these larger integer exponents the optimal decomposition depends on N
// (e.g. pow(x, 5) = (x*x)*(x*x)*x; pow(x, 8) = ((x*x)*(x*x))*((x*x)*(x*x))
// reused via a temporary, etc.), so the fix is suggestion-only and we do not
// emit a textual rewrite -- callers should manually pick the cheapest
// addition-chain for their case.
//
// The reasoning is identical to `pow-to-mul`: `pow` lowers to a
// transcendental sequence on every current GPU; a small chain of multiplies
// is strictly cheaper for integer exponents.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "pow-integer-decomposition";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_pow_name = "pow";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L' || c == 'u' ||
           c == 'U';
}

/// Parse `text` as a non-negative integer-valued numeric literal up to 999.
/// Returns the integer value, or `std::nullopt` if the literal is not
/// integer-valued or uses scientific notation.
[[nodiscard]] std::optional<int> parse_integer_literal(std::string_view text) noexcept {
    if (text.empty())
        return std::nullopt;
    std::size_t i = 0;
    if (text[i] == '+')
        ++i;
    if (i >= text.size())
        return std::nullopt;

    const std::size_t int_start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        ++i;
    if (i == int_start)
        return std::nullopt;
    const auto int_part = text.substr(int_start, i - int_start);

    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return std::nullopt;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return std::nullopt;
    while (i < text.size()) {
        if (!is_float_suffix(text[i]))
            return std::nullopt;
        ++i;
    }
    if (int_part.size() > 3U)
        return std::nullopt;
    int value = 0;
    for (const char d : int_part)
        value = (value * 10) + (d - '0');
    return value;
}

class PowIntegerDecomposition : public Rule {
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

    void on_node(const AstCursor& cursor, RuleContext& ctx) override {
        if (cursor.kind() != "call_expression")
            return;
        const auto bytes = cursor.source_bytes();
        const ::TSNode call = cursor.node();

        const ::TSNode fn = ::ts_node_child_by_field_name(call, "function", 8);
        if (node_kind(fn) != "identifier" || node_text(fn, bytes) != k_pow_name)
            return;

        const ::TSNode args = ::ts_node_child_by_field_name(call, "arguments", 9);
        if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 2U)
            return;

        const ::TSNode arg0 = ::ts_node_named_child(args, 0);
        const ::TSNode arg1 = ::ts_node_named_child(args, 1);
        if (::ts_node_is_null(arg0) || node_kind(arg1) != "number_literal")
            return;

        const auto exponent = parse_integer_literal(node_text(arg1, bytes));
        if (!exponent || *exponent < 5)
            return;

        // Skip pow(2.0, N) -- pow-base-two-to-exp2 owns that.
        if (node_kind(arg0) == "number_literal") {
            const auto base_val = parse_integer_literal(node_text(arg0, bytes));
            if (base_val && *base_val == 2)
                return;
        }

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        std::string msg = "`pow(x, ";
        // Render the integer exponent (up to 999).
        const int n = *exponent;
        if (n >= 100) {
            msg += static_cast<char>('0' + (n / 100));
            msg += static_cast<char>('0' + ((n / 10) % 10));
            msg += static_cast<char>('0' + (n % 10));
        } else if (n >= 10) {
            msg += static_cast<char>('0' + (n / 10));
            msg += static_cast<char>('0' + (n % 10));
        } else {
            msg += static_cast<char>('0' + n);
        }
        msg +=
            ".0)` should be replaced with a pow-by-squaring chain -- "
            "`pow` lowers to a transcendental sequence on every current GPU, "
            "but a handful of multiplies is strictly cheaper";
        diag.message = std::move(msg);

        // Suggestion-only: do not emit a TextEdit. The optimal decomposition
        // depends on N (Knuth addition chains), and naively repeating a
        // potentially-side-effecting base is unsafe.
        Fix fix;
        fix.machine_applicable = false;
        fix.description = std::string{
            "rewrite as a pow-by-squaring chain (e.g. `pow(x, 5)` --> "
            "`(x*x)*(x*x)*x`); choose an addition chain that minimises "
            "multiplications for your N"};
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_pow_integer_decomposition() {
    return std::make_unique<PowIntegerDecomposition>();
}

}  // namespace shader_clippy::rules
