// interlocked-float-bit-cast-trick
//
// Detects the hand-rolled `asuint` / sign-flip dance used to implement atomic
// min/max on floats before SM 6.6 added native `InterlockedMin/Max(float)`.
// Pattern: `InterlockedMin(uint_view, asuint(...))` or `InterlockedMax` with
// a manual sign-flip on the value.
//
// Stage: Ast. Textual detection on `call_expression` nodes whose function is
// `InterlockedMin` / `InterlockedMax` AND whose argument list contains
// `asuint(` or `asint(`.

#include <array>
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

constexpr std::string_view k_rule_id = "interlocked-float-bit-cast-trick";
constexpr std::string_view k_category = "workgroup";

constexpr std::array<std::string_view, 2> k_min_max{
    "InterlockedMin",
    "InterlockedMax",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        bool is_min_max = false;
        for (const auto name : k_min_max) {
            if (fn_text == name) {
                is_min_max = true;
                break;
            }
        }
        if (is_min_max) {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            const auto args_text = node_text(args, bytes);
            if (args_text.find("asuint(") != std::string_view::npos ||
                args_text.find("asint(") != std::string_view::npos) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{fn_text} +
                               " with asuint/asint round-trip -- SM 6.6 introduced "
                               "native InterlockedMin/Max on float; the bit-cast "
                               "trick is no longer needed";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "switch to the SM 6.6 native InterlockedMin/Max(float) -- "
                    "skips the asuint round-trip and the sign-flip ALU ops"};
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

class InterlockedFloatBitCastTrick : public Rule {
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

std::unique_ptr<Rule> make_interlocked_float_bit_cast_trick() {
    return std::make_unique<InterlockedFloatBitCastTrick>();
}

}  // namespace hlsl_clippy::rules
