// unpack-then-repack
//
// Detects round-trip pack/unpack patterns within a single expression tree:
// `pack8888(unpack8888(x))`, `pack_u8(unpack_u8u32(x))`,
// `f32tof16(f16tof32(x))`, etc. The pair is a no-op on the same lanes; the
// optimizer often DOES eliminate it but the code stays around as a runtime
// liability when the inner / outer casts vary.
//
// Stage: Ast. Trivially detectable: walk every `call_expression` whose
// function name is in our pack list and whose first argument is itself a
// call to the matching unpack (or vice-versa).

#include <array>
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

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "unpack-then-repack";
constexpr std::string_view k_category = "math";

struct PackPair {
    std::string_view outer;
    std::string_view inner;
};

constexpr std::array<PackPair, 8> k_pairs{{
    {"pack8888", "unpack8888"},
    {"unpack8888", "pack8888"},
    {"pack_u8", "unpack_u8u32"},
    {"pack_s8", "unpack_s8s32"},
    {"pack_clamp_u8", "unpack_u8u32"},
    {"pack_clamp_s8", "unpack_s8s32"},
    {"f32tof16", "f16tof32"},
    {"f16tof32", "f32tof16"},
}};

[[nodiscard]] std::string_view callee_name(::TSNode call, std::string_view bytes) noexcept {
    const auto fn = ::ts_node_child_by_field_name(call, "function", 8);
    return node_text(fn, bytes);
}

[[nodiscard]] ::TSNode first_arg(::TSNode call) noexcept {
    const auto args = ::ts_node_child_by_field_name(call, "arguments", 9);
    if (::ts_node_is_null(args))
        return {};
    if (::ts_node_named_child_count(args) == 0U)
        return {};
    return ::ts_node_named_child(args, 0);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto outer_name = callee_name(node, bytes);
        for (const auto& pair : k_pairs) {
            if (outer_name != pair.outer)
                continue;
            const auto arg = first_arg(node);
            if (::ts_node_is_null(arg))
                break;
            // Strip parens.
            ::TSNode inner = arg;
            if (node_kind(inner) == "parenthesized_expression" &&
                ::ts_node_named_child_count(inner) == 1U) {
                inner = ::ts_node_named_child(inner, 0);
            }
            if (node_kind(inner) != "call_expression")
                break;
            const auto inner_name = callee_name(inner, bytes);
            if (inner_name == pair.inner) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{"`"} + std::string{outer_name} + "(" +
                               std::string{inner_name} +
                               "(...))` is a round-trip with no intervening ALU -- the pair "
                               "is a no-op on the same lanes; remove both calls or carry the "
                               "value in its native form";
                ctx.emit(std::move(diag));
                break;
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class UnpackThenRepack : public Rule {
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

std::unique_ptr<Rule> make_unpack_then_repack() {
    return std::make_unique<UnpackThenRepack>();
}

}  // namespace shader_clippy::rules
