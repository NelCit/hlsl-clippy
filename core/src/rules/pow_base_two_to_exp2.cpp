// pow-base-two-to-exp2
//
// Detects `pow(2.0, x)` and suggests `exp2(x)`. `pow(2.0, x)` lowers to a
// log2/multiply/exp2 sequence on most GPU ISAs because the compiler does not
// know in general that the base reduces to a single exp2; calling `exp2`
// directly skips the log step and the constant multiply.
//
// The match is purely syntactic: the first argument to `pow` must be a number
// literal whose value is exactly 2 (with any conventional float spelling:
// `2`, `2.0`, `2.0f`, `2.0h`, ...).  The fix is machine-applicable.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/numeric_literal.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;
using util::is_numeric_literal_two;

constexpr std::string_view k_rule_id = "pow-base-two-to-exp2";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_pow_name = "pow";

class PowBaseTwoToExp2 : public Rule {
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
        if (node_kind(arg0) != "number_literal" || ::ts_node_is_null(arg1))
            return;
        if (!is_numeric_literal_two(node_text(arg0, bytes)))
            return;

        const auto exponent_text = node_text(arg1, bytes);
        if (exponent_text.empty())
            return;

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        diag.message = std::string{
            "`pow(2.0, x)` should be written as `exp2(x)` -- `exp2` is a single "
            "GPU instruction; `pow(2, x)` typically lowers to log2 + multiply + exp2"};

        Fix fix;
        fix.machine_applicable = true;
        fix.description = std::string{"replace `pow(2.0, x)` with `exp2(x)`"};
        TextEdit edit;
        edit.span = Span{.source = cursor.source_id(), .bytes = cursor.byte_range()};
        std::string replacement;
        replacement.reserve(exponent_text.size() + 6);
        replacement.append("exp2(");
        replacement.append(exponent_text);
        replacement.append(")");
        edit.replacement = std::move(replacement);
        fix.edits.push_back(std::move(edit));
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_pow_base_two_to_exp2() {
    return std::make_unique<PowBaseTwoToExp2>();
}

}  // namespace shader_clippy::rules
