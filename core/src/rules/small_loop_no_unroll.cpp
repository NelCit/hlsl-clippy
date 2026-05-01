// small-loop-no-unroll
//
// Detects `for (int i = 0; i < K; ++i)` loops with constant K <= a small
// threshold (default 8) and no `[unroll]` attribute. Fully unrolling such
// loops removes branch overhead and frees the iterator variable for the
// register allocator.
//
// Stage: Ast. Pure textual / AST detection on `for_statement` nodes.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "small-loop-no-unroll";
constexpr std::string_view k_category = "control-flow";
constexpr std::uint32_t k_small_threshold = 8;

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view prefix_text(std::string_view bytes, std::size_t start) noexcept {
    std::size_t i = start;
    while (i > 0) {
        const char c = bytes[i - 1];
        if (c == ';' || c == '}' || c == '{')
            break;
        --i;
    }
    return bytes.substr(i, start - i);
}

/// Try to extract `< K` or `<= K` constant bound from the condition text.
/// Returns 0 on failure.
[[nodiscard]] std::uint32_t parse_const_bound(std::string_view cond) noexcept {
    auto find_op = [&](std::string_view op) noexcept -> std::size_t { return cond.find(op); };
    std::size_t pos = find_op("<=");
    bool inclusive = true;
    if (pos == std::string_view::npos) {
        pos = find_op("<");
        inclusive = false;
    }
    if (pos == std::string_view::npos)
        return 0U;
    std::size_t i = pos + (inclusive ? 2U : 1U);
    while (i < cond.size() && (cond[i] == ' ' || cond[i] == '\t'))
        ++i;
    std::uint32_t n = 0;
    bool any = false;
    while (i < cond.size() && cond[i] >= '0' && cond[i] <= '9') {
        if (n < 1'000'000U)
            n = n * 10U + static_cast<std::uint32_t>(cond[i] - '0');
        any = true;
        ++i;
    }
    if (!any)
        return 0U;
    return inclusive ? n + 1U : n;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "for_statement") {
        // Try `condition` field first; fall back to scanning the for-statement
        // text for `<` operator.
        const auto cond = ::ts_node_child_by_field_name(node, "condition", 9);
        std::string_view cond_text = node_text(cond, bytes);
        if (cond_text.empty()) {
            cond_text = node_text(node, bytes);
        }
        const std::uint32_t bound = parse_const_bound(cond_text);
        if (bound > 0U && bound <= k_small_threshold) {
            const auto stmt_lo = static_cast<std::size_t>(::ts_node_start_byte(node));
            const auto prefix = prefix_text(bytes, stmt_lo);
            const bool has_unroll = prefix.find("[unroll") != std::string_view::npos;
            const bool has_loop_attr = prefix.find("[loop]") != std::string_view::npos;
            if (!has_unroll && !has_loop_attr) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{"small constant-bounded loop ("} +
                               std::to_string(bound) +
                               " iterations) without `[unroll]` -- the compiler usually "
                               "unrolls anyway, but the explicit hint guarantees the "
                               "rewrite and frees the iterator register";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "prefix the `for` with `[unroll]` (or `[unroll(N)]` for an "
                    "explicit factor) -- the compiler unrolls and frees the "
                    "iterator register on every modern IHV"};
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

class SmallLoopNoUnroll : public Rule {
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

std::unique_ptr<Rule> make_small_loop_no_unroll() {
    return std::make_unique<SmallLoopNoUnroll>();
}

}  // namespace hlsl_clippy::rules
