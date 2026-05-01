// acos-without-saturate
//
// Detects `acos(x)` / `asin(x)` calls whose argument is a `dot(...)`
// expression without a surrounding `saturate(...)` or `clamp(..., -1, 1)`
// guard. `dot()` of two normalised vectors is mathematically in [-1, 1] but
// rounding can land just outside the domain, producing NaN.
//
// Stage: Ast. The detection is purely textual on `call_expression` nodes.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "acos-without-saturate";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool starts_with_word(std::string_view text, std::string_view word) noexcept {
    if (text.size() < word.size())
        return false;
    if (text.substr(0, word.size()) != word)
        return false;
    if (text.size() == word.size())
        return true;
    const char c = text[word.size()];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "acos" || fn_text == "asin") {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) >= 1U) {
                const auto arg0 = ::ts_node_named_child(args, 0);
                const auto arg_text = node_text(arg0, bytes);
                // Look for a `dot(` call inside the argument text.
                const bool has_dot = arg_text.find("dot(") != std::string_view::npos ||
                                     arg_text.find("dot (") != std::string_view::npos;
                // Skip if the argument is already wrapped in saturate or
                // clamp; we treat any leading `saturate(` / `clamp(` token
                // (after optional `abs(`) as sufficient guard.
                std::size_t scan = 0;
                while (scan < arg_text.size() &&
                       (arg_text[scan] == ' ' || arg_text[scan] == '\t' || arg_text[scan] == '(' ||
                        arg_text[scan] == '+' || arg_text[scan] == '-')) {
                    ++scan;
                }
                const auto guarded = [&]() {
                    const auto rest = arg_text.substr(scan);
                    return starts_with_word(rest, "saturate") || starts_with_word(rest, "clamp");
                }();
                if (has_dot && !guarded) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{fn_text} +
                                   "(dot(...)) without saturate/clamp -- normalised dot " +
                                   "products can round outside [-1, 1] and produce NaN; " +
                                   "wrap the argument in clamp(x, -1.0, 1.0)";

                    Fix fix;
                    fix.machine_applicable = false;
                    fix.description = std::string{
                        "wrap the argument in clamp(x, -1.0, 1.0); on AMD/NVIDIA "
                        "this compiles to a free SAT modifier"};
                    diag.fixes.push_back(std::move(fix));

                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class AcosWithoutSaturate : public Rule {
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

std::unique_ptr<Rule> make_acos_without_saturate() {
    return std::make_unique<AcosWithoutSaturate>();
}

}  // namespace hlsl_clippy::rules
