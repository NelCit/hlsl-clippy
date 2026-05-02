// pow-const-squared
//
// Detects `pow(x, N)` where the exponent is an integer-valued literal in the
// set {2, 3, 4, 5} *and* the base is not itself a literal `2`. The latter
// guard is because `pow(2.0, x)` is the territory of `pow-base-two-to-exp2`
// (a separate Phase 2 rule); we don't want to fire on it twice.
//
// Carries a TextEdit fix per the docs (`applicability: machine-applicable`).
// The fix is machine-applicable when the base is a bare identifier — naively
// repeating a non-trivial expression would re-evaluate any side effects and
// is therefore downgraded to suggestion-grade. Exponent 5 produces
// `(x * x) * (x * x) * x` (pow-by-squaring); 2/3/4 mirror the rewrites
// pow-to-mul performs on the same call sites. When both rules fire, fix-all
// collapses to a single replace because both rules pick the same target span
// and the same replacement text.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "../parser_internal.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

using util::node_text;

constexpr std::string_view k_rule_id = "pow-const-squared";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_pow_name = "pow";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L' || c == 'u' ||
           c == 'U';
}

/// Skip a leading `+` if present.
[[nodiscard]] std::size_t skip_optional_plus(std::string_view text, std::size_t i) noexcept {
    return i < text.size() && text[i] == '+' ? i + 1U : i;
}

/// Read consecutive ASCII digits starting at `i`, returning the index just
/// past the last digit consumed.
[[nodiscard]] std::size_t read_digits(std::string_view text, std::size_t i) noexcept {
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    return i;
}

/// Skip a `.0...0` fractional part if present. Returns `std::nullopt` if a
/// non-zero digit follows the dot (the literal is not integer-valued).
[[nodiscard]] std::optional<std::size_t> skip_zero_fraction(std::string_view text,
                                                            std::size_t i) noexcept {
    if (i >= text.size() || text[i] != '.') {
        return i;
    }
    ++i;
    while (i < text.size() && text[i] == '0') {
        ++i;
    }
    if (i < text.size() && text[i] >= '1' && text[i] <= '9') {
        return std::nullopt;
    }
    return i;
}

/// Skip every trailing C/HLSL float suffix (f, F, h, H, l, L, u, U). Returns
/// `std::nullopt` if any non-suffix character is found.
[[nodiscard]] std::optional<std::size_t> skip_suffixes(std::string_view text,
                                                       std::size_t i) noexcept {
    while (i < text.size()) {
        if (!is_float_suffix(text[i])) {
            return std::nullopt;
        }
        ++i;
    }
    return i;
}

/// Parse a numeric literal. Returns the integer value when `text` represents
/// a non-negative integer-valued real (`2`, `2.0`, `2.0f`, `3`, `40.000h`, …)
/// in `[0, 99]`, otherwise `std::nullopt`. The exponent form (`2e0`) is not
/// accepted — it's rare in shader code and rejecting it keeps the rule
/// conservative.
[[nodiscard]] std::optional<int> parse_integer_literal(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    std::size_t i = skip_optional_plus(text, 0);
    if (i >= text.size()) {
        return std::nullopt;
    }

    const std::size_t int_start = i;
    i = read_digits(text, i);
    if (int_start == i) {
        return std::nullopt;  // No digits (e.g., `.5`).
    }
    const std::string_view int_part = text.substr(int_start, i - int_start);

    const auto after_frac = skip_zero_fraction(text, i);
    if (!after_frac) {
        return std::nullopt;
    }
    i = *after_frac;

    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        return std::nullopt;
    }

    const auto after_suffix = skip_suffixes(text, i);
    if (!after_suffix) {
        return std::nullopt;
    }

    if (int_part.size() > 2U) {
        return std::nullopt;
    }
    int value = 0;
    for (const char d : int_part) {
        value = (value * 10) + (d - '0');
    }
    return value;
}

/// Slice the source bytes covered by `node`. Returns an empty view if the
/// node's reported byte range is outside the buffer.
/// True if `node` exists and matches the named-node type `expected`.
[[nodiscard]] bool node_kind_is(::TSNode node, std::string_view expected) noexcept {
    if (ts_node_is_null(node)) {
        return false;
    }
    const char* type = ts_node_type(node);
    return type != nullptr && std::string_view{type} == expected;
}

/// Build the textual replacement for `pow(x, N)` with N in {2, 3, 4, 5}.
/// Matches pow-to-mul's spelling on overlapping exponents so fix-all
/// collapses duplicate edits into one.
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
        case 5: {
            std::string r;
            r.reserve(base.size() * 5 + 16);
            r.append("(");
            r.append(base);
            r.append(" * ");
            r.append(base);
            r.append(") * (");
            r.append(base);
            r.append(" * ");
            r.append(base);
            r.append(") * ");
            r.append(base);
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
            msg += "`(x * x) * (x * x)` (one multiplication saved via squaring)";
            break;
        case 5:
            msg += "`(x * x) * (x * x) * x` (pow-by-squaring; 3 multiplies vs. transcendental)";
            break;
        default:
            msg += "repeated multiplication";
            break;
    }
    msg += " — `pow` lowers to a transcendental sequence on every current GPU";
    return msg;
}

class PowConstSquared : public Rule {
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
        if (cursor.kind() != "call_expression") {
            return;
        }
        const std::string_view bytes = cursor.source_bytes();
        const auto match = match_pow_call(cursor.node(), bytes);
        if (!match) {
            return;
        }

        // Build the diagnostic. Span the entire call expression so the caret
        // underlines the whole `pow(...)` site rather than just the exponent.
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        diag.message = make_message(match->exponent);

        // Attach a Fix when the base text is non-empty (always true after
        // matching). machine-applicable iff the base is a bare identifier;
        // otherwise suggestion-grade because repeating an arbitrary
        // expression re-evaluates side effects (e.g. `pow(load(p), 5.0)`).
        Fix fix;
        fix.machine_applicable = match->base_is_simple_identifier;
        fix.description = match->base_is_simple_identifier
                              ? std::string{"replace `pow(x, N.0)` with repeated multiplication"}
                              : std::string{
                                    "replace with repeated multiplication; verify the base "
                                    "expression is side-effect-free before applying"};
        TextEdit edit;
        edit.span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        edit.replacement = make_replacement(match->base_text, match->exponent);
        fix.edits.push_back(std::move(edit));
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }

private:
    struct PowMatch {
        int exponent;
        std::string_view base_text;
        bool base_is_simple_identifier;
    };

    /// If `call` is a `pow(x, N.0)` site whose exponent is in {2, 3, 4, 5} and
    /// whose base is *not* a literal 2, return the integer exponent and the
    /// base's source-text view.
    [[nodiscard]] static std::optional<PowMatch> match_pow_call(::TSNode call,
                                                                std::string_view bytes) noexcept {
        const ::TSNode fn = ts_node_child_by_field_name(call, "function", 8);
        if (!node_kind_is(fn, "identifier") || node_text(fn, bytes) != k_pow_name) {
            return std::nullopt;
        }

        const ::TSNode args = ts_node_child_by_field_name(call, "arguments", 9);
        if (ts_node_is_null(args) || ts_node_named_child_count(args) != 2U) {
            return std::nullopt;
        }

        const ::TSNode arg0 = ts_node_named_child(args, 0);
        const ::TSNode arg1 = ts_node_named_child(args, 1);
        if (!node_kind_is(arg1, "number_literal")) {
            return std::nullopt;
        }

        const auto exponent = parse_integer_literal(node_text(arg1, bytes));
        if (!exponent || *exponent < 2 || *exponent > 5) {
            return std::nullopt;
        }

        // Skip `pow(2.0, x)` — that's `pow-base-two-to-exp2` territory.
        if (node_kind_is(arg0, "number_literal")) {
            const auto base = parse_integer_literal(node_text(arg0, bytes));
            if (base && *base == 2) {
                return std::nullopt;
            }
        }

        const auto base_text = node_text(arg0, bytes);
        if (base_text.empty()) {
            return std::nullopt;
        }
        return PowMatch{
            .exponent = *exponent,
            .base_text = base_text,
            .base_is_simple_identifier = node_kind_is(arg0, "identifier"),
        };
    }
};

}  // namespace

std::unique_ptr<Rule> make_pow_const_squared() {
    return std::make_unique<PowConstSquared>();
}

}  // namespace hlsl_clippy::rules
