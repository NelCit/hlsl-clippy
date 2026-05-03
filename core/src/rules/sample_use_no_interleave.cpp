// sample-use-no-interleave
//
// Detects a `Sample()` whose result is consumed within 3 dependent
// statements without intervening compute. Best-effort heuristic --
// modelled on Nsight's "Warp Stalled by L1 Long Scoreboard" warning:
// interleave compute between sample and use to hide texture-cache
// miss latency.
//
// Stage: ControlFlow (uses CFG only to gate "we have a function
// body to walk"; the actual heuristic is a textual sliding window
// over statements in the same compound block).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "sample-use-no-interleave";
constexpr std::string_view k_category = "memory";
constexpr std::size_t k_window_statements = 3U;

constexpr std::array<std::string_view, 5> k_sample_names{
    "Sample",
    "SampleLevel",
    "SampleGrad",
    "SampleBias",
    "SampleCmp",
};

[[nodiscard]] bool callee_is_sample(::TSNode call, std::string_view bytes) noexcept {
    const auto fn = ::ts_node_child_by_field_name(call, "function", 8);
    // Sample calls are `<texture>.Sample*(...)`. The function field is a
    // `field_expression` whose `field` is the name; pull it out.
    if (node_kind(fn) == "field_expression") {
        const auto field = ::ts_node_child_by_field_name(fn, "field", 5);
        const auto field_text = node_text(field, bytes);
        for (const auto name : k_sample_names) {
            if (field_text == name) {
                return true;
            }
        }
    }
    return false;
}

/// True when `text` mentions `target` as a whole-word identifier.
[[nodiscard]] bool mentions_identifier(std::string_view text, std::string_view target) noexcept {
    if (target.empty()) {
        return false;
    }
    std::size_t pos = 0U;
    while (pos < text.size()) {
        const auto found = text.find(target, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool left_ok = (found == 0U) || !is_id_char(text[found - 1U]);
        const std::size_t end = found + target.size();
        const bool right_ok = (end >= text.size()) || !is_id_char(text[end]);
        if (left_ok && right_ok) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// Extract the declarator identifier from a `declaration` whose initializer
/// is a Sample call. Returns an empty view when the shape doesn't match.
[[nodiscard]] std::string_view get_decl_name(::TSNode decl,
                                             std::string_view bytes,
                                             ::TSNode& init_call) noexcept {
    init_call = ::TSNode{};
    if (node_kind(decl) != "declaration") {
        return {};
    }
    // Walk children for an init_declarator whose value is a call_expression.
    const std::uint32_t cnt = ::ts_node_named_child_count(decl);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        const auto child = ::ts_node_named_child(decl, i);
        if (node_kind(child) == "init_declarator") {
            const auto name = ::ts_node_child_by_field_name(child, "declarator", 10);
            const auto val = ::ts_node_child_by_field_name(child, "value", 5);
            if (node_kind(val) == "call_expression" && callee_is_sample(val, bytes)) {
                init_call = val;
                return node_text(name, bytes);
            }
        }
    }
    return {};
}

void scan_compound(::TSNode block, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(block)) {
        return;
    }
    if (node_kind(block) != "compound_statement") {
        return;
    }
    const std::uint32_t cnt = ::ts_node_named_child_count(block);
    std::vector<::TSNode> stmts;
    stmts.reserve(cnt);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        stmts.push_back(::ts_node_named_child(block, i));
    }
    for (std::size_t i = 0; i < stmts.size(); ++i) {
        ::TSNode init_call{};
        const auto name = get_decl_name(stmts[i], bytes, init_call);
        if (name.empty() || ::ts_node_is_null(init_call)) {
            continue;
        }
        // Look at the next k_window_statements statements; if any of them
        // mention `name`, we fire (sample + use within window without
        // intervening compute).
        std::size_t scanned = 0U;
        for (std::size_t j = i + 1U; j < stmts.size() && scanned < k_window_statements; ++j) {
            const auto stmt_text = node_text(stmts[j], bytes);
            if (mentions_identifier(stmt_text, name)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(init_call)};
                diag.message = std::string{"(suggestion) `Sample()` result `"} + std::string{name} +
                               "` is consumed within the next " + std::to_string(scanned + 1U) +
                               " statement(s) -- interleave compute between sample and use to hide "
                               "the texture-cache miss latency (Nsight: \"Warp Stalled by L1 Long "
                               "Scoreboard\")";
                ctx.emit(std::move(diag));
                break;
            }
            ++scanned;
        }
    }
    // Recurse into nested compound statements.
    for (const auto s : stmts) {
        const std::uint32_t inner_cnt = ::ts_node_child_count(s);
        for (std::uint32_t k = 0; k < inner_cnt; ++k) {
            const auto child = ::ts_node_child(s, k);
            if (node_kind(child) == "compound_statement") {
                scan_compound(child, bytes, tree, ctx);
            } else {
                // One level deeper -- e.g. for-statement body.
                const std::uint32_t inner2 = ::ts_node_child_count(child);
                for (std::uint32_t m = 0; m < inner2; ++m) {
                    const auto cc = ::ts_node_child(child, m);
                    if (node_kind(cc) == "compound_statement") {
                        scan_compound(cc, bytes, tree, ctx);
                    }
                }
            }
        }
    }
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto body = ::ts_node_child_by_field_name(node, "body", 4);
        if (node_kind(body) == "compound_statement") {
            scan_compound(body, bytes, tree, ctx);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SampleUseNoInterleave : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree,
                [[maybe_unused]] const ControlFlowInfo& cfg,
                RuleContext& ctx) override {
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_sample_use_no_interleave() {
    return std::make_unique<SampleUseNoInterleave>();
}

}  // namespace shader_clippy::rules
