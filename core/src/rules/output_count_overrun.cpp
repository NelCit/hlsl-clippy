// output-count-overrun
//
// Detects `SetMeshOutputCounts(v, p)` calls whose `v` or `p` argument
// (when a literal) exceeds the declared `out vertices ... arr[V]` /
// `out primitives ... arr[P]` ceiling. The mesh shader spec is undefined
// for output counts above the array sizes; on most IHVs this scribbles
// past the workgroup-output buffer and corrupts neighbouring meshlets.
//
// Stage: ControlFlow. The rule scans the AST for output-array declarations
// to recover the (V, P) ceilings, then walks reachable `SetMeshOutputCounts`
// calls inside each function and compares literal arguments against the
// recovered ceilings.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "output-count-overrun";
constexpr std::string_view k_category = "mesh";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] std::optional<std::uint32_t> parse_uint_literal(std::string_view s) noexcept {
    s = trim(s);
    if (s.empty())
        return std::nullopt;
    // Strip 'u' / 'U' suffix.
    if (s.back() == 'u' || s.back() == 'U')
        s.remove_suffix(1U);
    std::uint32_t v = 0U;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return std::nullopt;
        v = v * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return v;
}

[[nodiscard]] std::optional<std::uint32_t> find_output_cap(std::string_view bytes,
                                                           std::string_view kw) noexcept {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(kw, pos);
        if (found == std::string_view::npos)
            return std::nullopt;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        const auto lb = bytes.find('[', end);
        if (lb == std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto rb = bytes.find(']', lb + 1U);
        if (rb == std::string_view::npos) {
            pos = end;
            continue;
        }
        if (bytes.substr(end, lb - end).find(';') != std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto digits = trim(bytes.substr(lb + 1U, rb - lb - 1U));
        std::uint32_t v = 0U;
        bool ok = !digits.empty();
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            v = v * 10U + static_cast<std::uint32_t>(c - '0');
        }
        if (ok) {
            return v;
        }
        pos = rb + 1U;
    }
    return std::nullopt;
}

class OutputCountOverrun : public Rule {
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
        const auto bytes = tree.source_bytes();
        if (bytes.find("SetMeshOutputCounts") == std::string_view::npos)
            return;
        const auto v_cap = find_output_cap(bytes, "out vertices");
        // Primitives can be declared as `out indices` or `out primitives`.
        auto p_cap = find_output_cap(bytes, "out primitives");
        if (!p_cap.has_value()) {
            p_cap = find_output_cap(bytes, "out indices");
        }
        if (!v_cap.has_value() && !p_cap.has_value())
            return;

        // Walk the AST for SetMeshOutputCounts calls.
        std::vector<::TSNode> calls;
        std::vector<::TSNode> stack;
        stack.push_back(::ts_tree_root_node(tree.raw_tree()));
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            if (::ts_node_is_null(node))
                continue;
            if (node_kind(node) == "call_expression") {
                const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
                const auto fn_text = node_text(fn, bytes);
                if (fn_text == "SetMeshOutputCounts") {
                    calls.push_back(node);
                }
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                stack.push_back(::ts_node_child(node, i));
            }
        }

        for (const auto call : calls) {
            const auto call_span = Span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(call),
            };
            // Ensure the call is reachable in the CFG (not dead code).
            if (!rules::util::block_for(cfg, call_span).has_value())
                continue;
            const auto args = ::ts_node_child_by_field_name(call, "arguments", 9);
            if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) < 2U)
                continue;
            const auto v_arg = node_text(::ts_node_named_child(args, 0), bytes);
            const auto p_arg = node_text(::ts_node_named_child(args, 1), bytes);
            const auto v_lit = parse_uint_literal(v_arg);
            const auto p_lit = parse_uint_literal(p_arg);
            const bool v_overrun = v_cap.has_value() && v_lit.has_value() && *v_lit > *v_cap;
            const bool p_overrun = p_cap.has_value() && p_lit.has_value() && *p_lit > *p_cap;
            if (!v_overrun && !p_overrun)
                continue;

            std::string msg = "`SetMeshOutputCounts(";
            msg += v_lit.has_value() ? std::to_string(*v_lit) : std::string{v_arg};
            msg += ", ";
            msg += p_lit.has_value() ? std::to_string(*p_lit) : std::string{p_arg};
            msg += ")` exceeds the declared output ceiling (";
            msg += v_cap.has_value() ? std::to_string(*v_cap) : std::string{"?"};
            msg += " vertices, ";
            msg += p_cap.has_value() ? std::to_string(*p_cap) : std::string{"?"};
            msg +=
                " primitives) -- writes past the workgroup-output buffer have undefined "
                "semantics on every IHV";

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = call_span;
            diag.message = std::move(msg);
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_output_count_overrun() {
    return std::make_unique<OutputCountOverrun>();
}

}  // namespace hlsl_clippy::rules
