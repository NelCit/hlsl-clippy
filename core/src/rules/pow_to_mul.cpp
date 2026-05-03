// pow-to-mul
//
// Detects `pow(x, 2.0)`, `pow(x, 3.0)`, and `pow(x, 4.0)` where the exponent
// is a small integer-valued literal and suggests rewriting as repeated
// multiplication:
//
//   pow(x, 2.0)  -->  x * x
//   pow(x, 3.0)  -->  x * x * x
//   pow(x, 4.0)  -->  (x * x) * (x * x)
//
// `pow` lowers to a transcendental sequence (exp2 + log2 + multiply) on every
// current GPU; for tiny integer exponents, repeated multiplication is strictly
// cheaper.
//
// The fix is machine-applicable when the base `x` is a simple identifier
// (where the textual repetition is unambiguous and side-effect-free). For
// complex base expressions the fix is downgraded to suggestion-only — naively
// repeating a non-trivial expression would re-evaluate it (and any side
// effects) multiple times.
//
// Note: the existing `pow-const-squared` rule also covers exponent 2; both
// rules can fire on `pow(x, 2.0)`. Users can disable whichever they prefer
// via `.shader-clippy.toml`.

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

constexpr std::string_view k_rule_id = "pow-to-mul";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_pow_name = "pow";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L' || c == 'u' ||
           c == 'U';
}

/// Parse `text` as a non-negative integer-valued numeric literal in [0, 99].
/// Returns the integer value, or `std::nullopt` if the literal is not
/// integer-valued (e.g. `2.5`) or uses scientific notation.
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
    if (int_part.size() > 2U)
        return std::nullopt;
    int value = 0;
    for (const char d : int_part)
        value = (value * 10) + (d - '0');
    return value;
}

/// Build the textual replacement for `pow(x, N)` with N in {2, 3, 4}.
[[nodiscard]] std::string make_replacement(std::string_view base, int exponent) {
    switch (exponent) {
        case 2: {
            std::string r;
            r.reserve(base.size() * 2 + 3);
            r.append(base);
            r.append(" * ");
            r.append(base);
            return r;
        }
        case 3: {
            std::string r;
            r.reserve(base.size() * 3 + 6);
            r.append(base);
            r.append(" * ");
            r.append(base);
            r.append(" * ");
            r.append(base);
            return r;
        }
        case 4: {
            std::string r;
            r.reserve(base.size() * 4 + 12);
            r.append("(");
            r.append(base);
            r.append(" * ");
            r.append(base);
            r.append(") * (");
            r.append(base);
            r.append(" * ");
            r.append(base);
            r.append(")");
            return r;
        }
        default:
            return std::string{base};
    }
}

[[nodiscard]] std::string make_message(int exponent) {
    std::string msg = "`pow(x, ";
    msg += static_cast<char>('0' + exponent);
    msg += ".0)` should be written as ";
    switch (exponent) {
        case 2:
            msg += "`x * x`";
            break;
        case 3:
            msg += "`x * x * x`";
            break;
        case 4:
            msg += "`(x * x) * (x * x)`";
            break;
        default:
            msg += "repeated multiplication";
            break;
    }
    msg += " -- `pow` lowers to a transcendental sequence on every current GPU";
    return msg;
}

class PowToMul : public Rule {
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
        if (!exponent || *exponent < 2 || *exponent > 4)
            return;

        // Skip pow(2.0, x) -- that's pow-base-two-to-exp2 territory and not
        // a candidate for repeated multiplication of the base.
        if (node_kind(arg0) == "number_literal") {
            const auto base_val = parse_integer_literal(node_text(arg0, bytes));
            if (base_val && *base_val == 2)
                return;
        }

        const auto base_text = node_text(arg0, bytes);
        if (base_text.empty())
            return;

        const bool base_is_simple_identifier = (node_kind(arg0) == "identifier");

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        diag.message = make_message(*exponent);

        Fix fix;
        fix.machine_applicable = base_is_simple_identifier;
        fix.description = base_is_simple_identifier
                              ? std::string{"replace `pow(x, N.0)` with repeated multiplication"}
                              : std::string{
                                    "replace with repeated multiplication; verify the base "
                                    "expression is side-effect-free before applying"};
        TextEdit edit;
        edit.span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        edit.replacement = make_replacement(base_text, *exponent);
        fix.edits.push_back(std::move(edit));
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_pow_to_mul() {
    return std::make_unique<PowToMul>();
}

}  // namespace shader_clippy::rules
