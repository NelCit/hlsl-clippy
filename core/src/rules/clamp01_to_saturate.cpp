// clamp01-to-saturate
//
// Detects `clamp(x, 0.0, 1.0)` calls (or any literal-zero / literal-one form)
// and replaces them with `saturate(x)` via a machine-applicable fix. On every
// modern GPU, `clamp(x, 0, 1)` lowers to a min/max pair while `saturate(x)`
// folds into a free output modifier, so the rewrite is a strict win.
//
// Both bounds must be parseable as numeric literals that round-trip to
// exactly 0.0 and 1.0. Anything else (variables, named constants, integer
// suffixes that change the type — `0u`, `1u` — or non-zero/non-one values)
// is left alone. This is intentional: the rule only fires when we can prove
// the rewrite is observationally identical to the original.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "query/query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "clamp01-to-saturate";
constexpr std::string_view k_category = "saturate-redundancy";
constexpr std::string_view k_clamp_name = "clamp";

constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (identifier) @fn
        arguments: (argument_list
            (_) @x
            (number_literal) @lo
            (number_literal) @hi)) @call
)";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

/// Classification of a literal's integer part. We only ever compare against
/// 0 and 1 in this rule, so distinguishing those two from "everything else" is
/// enough.
enum class IntegerPart : std::uint8_t {
    Zero,
    One,
    Other,
};

/// Read consecutive ASCII digits at `i`, returning the new index past the
/// last digit and (out) the integer-part classification.
[[nodiscard]] std::size_t scan_int_part(std::string_view text,
                                        std::size_t i,
                                        IntegerPart& kind) noexcept {
    const std::size_t int_start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    if (i == int_start) {
        kind = IntegerPart::Other;  // No integer-part digits at all.
        return i;
    }
    const auto int_text = text.substr(int_start, i - int_start);
    std::size_t k = 0;
    while (k < int_text.size() && int_text[k] == '0') {
        ++k;
    }
    const auto trimmed = int_text.substr(k);
    if (trimmed.empty()) {
        kind = IntegerPart::Zero;
    } else if (trimmed == "1") {
        kind = IntegerPart::One;
    } else {
        kind = IntegerPart::Other;
    }
    return i;
}

/// Skip a `.0...0` fractional part. Returns `std::nullopt` when a non-zero
/// fractional digit appears (the literal can't be 0 or 1).
[[nodiscard]] std::optional<std::size_t> scan_zero_fraction(std::string_view text,
                                                            std::size_t i) noexcept {
    if (i >= text.size() || text[i] != '.') {
        return i;
    }
    ++i;  // past '.'
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        if (text[i] != '0') {
            return std::nullopt;
        }
        ++i;
    }
    return i;
}

/// Skip an optional `e[sign]digits` exponent. Returns `std::nullopt` when the
/// exponent is malformed or non-zero (which would shift the value).
[[nodiscard]] std::optional<std::size_t> scan_zero_exponent(std::string_view text,
                                                            std::size_t i) noexcept {
    if (i >= text.size() || (text[i] != 'e' && text[i] != 'E')) {
        return i;
    }
    ++i;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        ++i;
    }
    const std::size_t exp_start = i;
    long exp_value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        exp_value = (exp_value * 10) + (text[i] - '0');
        ++i;
    }
    if (i == exp_start || exp_value != 0) {
        return std::nullopt;
    }
    return i;
}

/// Skip every trailing float suffix. Returns `std::nullopt` if a non-suffix
/// (e.g. `u`, `U` for unsigned-integer literals) is found.
[[nodiscard]] std::optional<std::size_t> scan_float_suffix(std::string_view text,
                                                           std::size_t i) noexcept {
    while (i < text.size()) {
        if (!is_float_suffix(text[i])) {
            return std::nullopt;
        }
        ++i;
    }
    return i;
}

/// Parse `text` as a literal that round-trips exactly to `target` (which is
/// either 0.0 or 1.0 for this rule's purposes). Accepts `0`, `0.0`, `0.0f`,
/// `0.f`, `1`, `1.0`, `1.0f`, `+1.0`, `1e0`. Rejects integer suffixes
/// (`0u`, `1u`) and any non-zero/non-one value.
[[nodiscard]] bool literal_equals(std::string_view text, double target) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (text[i] == '+') {
        ++i;
    }

    IntegerPart int_kind = IntegerPart::Other;
    i = scan_int_part(text, i, int_kind);

    const auto after_frac = scan_zero_fraction(text, i);
    if (!after_frac.has_value()) {
        return false;
    }
    i = *after_frac;

    const auto after_exp = scan_zero_exponent(text, i);
    if (!after_exp.has_value()) {
        return false;
    }
    i = *after_exp;

    const auto after_suffix = scan_float_suffix(text, i);
    if (!after_suffix.has_value()) {
        return false;
    }

    if (target == 0.0) {
        return int_kind == IntegerPart::Zero;
    }
    return int_kind == IntegerPart::One;
}

class Clamp01ToSaturate : public Rule {
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
        auto compiled = query::Query::compile(tree.language(), k_pattern);
        if (!compiled.has_value()) {
            Diagnostic diag;
            diag.code = std::string{"clippy::query-compile"};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            diag.message = std::string{"failed to compile clamp01-to-saturate query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(compiled.value(),
                   ::ts_tree_root_node(tree.raw_tree()),
                   [&](const query::QueryMatch& match) {
                       const ::TSNode fn = match.capture("fn");
                       const ::TSNode x = match.capture("x");
                       const ::TSNode lo = match.capture("lo");
                       const ::TSNode hi = match.capture("hi");
                       const ::TSNode call = match.capture("call");
                       if (::ts_node_is_null(fn) || ::ts_node_is_null(x) || ::ts_node_is_null(lo) ||
                           ::ts_node_is_null(hi) || ::ts_node_is_null(call)) {
                           return;
                       }
                       if (tree.text(fn) != k_clamp_name) {
                           return;
                       }
                       if (!literal_equals(tree.text(lo), 0.0)) {
                           return;
                       }
                       if (!literal_equals(tree.text(hi), 1.0)) {
                           return;
                       }

                       const auto call_range = tree.byte_range(call);
                       const auto x_text = tree.text(x);

                       Diagnostic diag;
                       diag.code = std::string{k_rule_id};
                       diag.severity = Severity::Warning;
                       diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                       diag.message = std::string{
                           "`clamp(x, 0, 1)` is `saturate(x)` — saturate folds into a free "
                           "output modifier on AMD/NVIDIA/Intel; clamp lowers to a min/max pair"};

                       Fix fix;
                       fix.description = std::string{"replace clamp(x, 0, 1) with saturate(x)"};
                       TextEdit edit;
                       edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                       edit.replacement = std::string{"saturate("} + std::string{x_text} + ")";
                       fix.edits.push_back(std::move(edit));
                       diag.fixes.push_back(std::move(fix));

                       ctx.emit(std::move(diag));
                   });
    }
};

}  // namespace

std::unique_ptr<Rule> make_clamp01_to_saturate() {
    return std::make_unique<Clamp01ToSaturate>();
}

}  // namespace hlsl_clippy::rules
