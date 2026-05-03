// primcount-overrun-in-conditional-cf
//
// Detects a `SetMeshOutputCounts(N, M)` call inside a mesh-shader body
// reachable from non-thread-uniform control flow, OR with `N` / `M`
// arguments classified as `Divergent` by the uniformity oracle. The HLSL
// mesh-shader contract requires the call to happen exactly once, with
// thread-uniform values, before any vertex / primitive write -- divergent
// counts produce undefined behaviour on every IHV.
//
// Stage: ControlFlow.
//
// Detection plan:
//   1. Walk the AST for `call_expression` nodes whose function identifier
//      is `SetMeshOutputCounts`.
//   2. For each match, ask the uniformity oracle the uniformity of the
//      first and second argument spans via `util::is_divergent`.
//   3. Use `util::inside_divergent_cf` to detect the "reachable from
//      non-thread-uniform CF" flavour of the bug.
//   4. Emit on the call span when either (a) any argument is divergent or
//      (b) the call is enclosed by a divergent branch.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/uniformity.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "primcount-overrun-in-conditional-cf";
constexpr std::string_view k_category = "mesh";
constexpr std::string_view k_callee = "SetMeshOutputCounts";

/// Collect the byte spans of the comma-separated top-level arguments inside
/// the parenthesised `argument_list` node. Nested parens / brackets are
/// tracked so that a comma inside `f(a, b)` is not split.
void collect_argument_spans(::TSNode arg_list, std::string_view bytes, std::vector<ByteSpan>& out) {
    if (::ts_node_is_null(arg_list)) {
        return;
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(arg_list));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(arg_list));
    if (hi <= lo + 1U || hi > bytes.size()) {
        return;
    }
    // Skip leading '(' and trailing ')'.
    std::uint32_t cur = lo + 1U;
    const std::uint32_t end = hi - 1U;
    std::uint32_t arg_start = cur;
    int depth = 0;
    for (std::uint32_t i = cur; i < end; ++i) {
        const char c = bytes[i];
        if (c == '(' || c == '[' || c == '<') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '>') {
            if (depth > 0) {
                --depth;
            }
        } else if (c == ',' && depth == 0) {
            out.push_back(ByteSpan{.lo = arg_start, .hi = i});
            arg_start = i + 1U;
        }
    }
    if (arg_start < end) {
        out.push_back(ByteSpan{.lo = arg_start, .hi = end});
    }
}

/// Strip leading/trailing whitespace from a byte span. Half-open.
[[nodiscard]] ByteSpan trim_span(ByteSpan span, std::string_view bytes) noexcept {
    while (span.lo < span.hi && span.lo < bytes.size() &&
           (bytes[span.lo] == ' ' || bytes[span.lo] == '\t' || bytes[span.lo] == '\n' ||
            bytes[span.lo] == '\r')) {
        ++span.lo;
    }
    while (span.hi > span.lo && span.hi - 1U < bytes.size() &&
           (bytes[span.hi - 1U] == ' ' || bytes[span.hi - 1U] == '\t' ||
            bytes[span.hi - 1U] == '\n' || bytes[span.hi - 1U] == '\r')) {
        --span.hi;
    }
    return span;
}

void walk(::TSNode node,
          std::string_view bytes,
          const AstTree& tree,
          const ControlFlowInfo& cfg,
          RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn_field = ::ts_node_child_by_field_name(node, "function", 8U);
        const auto fn_name = node_text(fn_field, bytes);
        if (fn_name == k_callee) {
            const auto args_field = ::ts_node_child_by_field_name(node, "arguments", 9U);
            std::vector<ByteSpan> arg_spans;
            collect_argument_spans(args_field, bytes, arg_spans);
            const auto call_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
            const auto call_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
            const Span call_span{
                .source = tree.source_id(),
                .bytes = ByteSpan{.lo = call_lo, .hi = call_hi},
            };
            bool any_divergent_arg = false;
            for (const auto raw : arg_spans) {
                const auto trimmed = trim_span(raw, bytes);
                if (trimmed.empty()) {
                    continue;
                }
                const Span arg_span{
                    .source = tree.source_id(),
                    .bytes = trimmed,
                };
                if (util::is_divergent(cfg, arg_span)) {
                    any_divergent_arg = true;
                    break;
                }
            }
            const bool inside_div = util::inside_divergent_cf(cfg, call_span);
            if (any_divergent_arg || inside_div) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = call_span;
                if (any_divergent_arg) {
                    diag.message = std::string{
                        "`SetMeshOutputCounts(...)` argument is wave-divergent -- the contract "
                        "requires thread-uniform vertex / primitive counts; UB on RDNA / Ada / "
                        "Xe-HPG (silent over-count writes corrupt downstream meshlet state)"};
                } else {
                    diag.message = std::string{
                        "`SetMeshOutputCounts(...)` reached from non-thread-uniform control "
                        "flow -- the contract requires the call to dominate every primitive "
                        "write with thread-uniform counts; UB on RDNA / Ada / Xe-HPG"};
                }
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, cfg, ctx);
    }
}

class PrimcountOverrunInConditionalCf : public Rule {
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

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, cfg, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_primcount_overrun_in_conditional_cf() {
    return std::make_unique<PrimcountOverrunInConditionalCf>();
}

}  // namespace shader_clippy::rules
