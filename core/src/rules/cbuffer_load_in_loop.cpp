// cbuffer-load-in-loop
//
// Detects cbuffer field reads inside a loop whose access expression is
// loop-invariant. The constant-cache load is cheap but not free, and the
// compiler does not always CSE across iteration boundaries; the developer
// can hoist the read into a local once per loop entry.
//
// Stage: ControlFlow. Uses `cfg_query::inside_loop` +
// `light_dataflow::loop_invariant_expr` to gate the firing on `field_expression`
// nodes whose receiver is a top-level cbuffer-block name. The cbuffer-name
// detection is purely textual (we list every identifier following a
// `cbuffer` keyword).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/light_dataflow.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "cbuffer-load-in-loop";
constexpr std::string_view k_category = "bindings";

/// Collect all identifier tokens immediately following a `cbuffer` keyword in
/// the source. The identifier is the cbuffer block's name in HLSL syntax.
[[nodiscard]] std::unordered_set<std::string> collect_cbuffer_names(std::string_view bytes) {
    std::unordered_set<std::string> out;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto found = bytes.find("cbuffer", pos);
        if (found == std::string_view::npos)
            return out;
        // Confirm word-boundary on left.
        if (found > 0 && is_id_char(bytes[found - 1])) {
            pos = found + 1;
            continue;
        }
        std::size_t i = found + std::string_view{"cbuffer"}.size();
        if (i < bytes.size() && is_id_char(bytes[i])) {
            // Not a keyword; bare prefix.
            pos = i;
            continue;
        }
        // Skip whitespace.
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        std::size_t name_start = i;
        while (i < bytes.size() && is_id_char(bytes[i]))
            ++i;
        if (i > name_start) {
            out.emplace(std::string{bytes.substr(name_start, i - name_start)});
        }
        pos = i;
    }
    return out;
}

void collect_field_reads(::TSNode node,
                         std::string_view bytes,
                         const std::unordered_set<std::string>& cb_names,
                         std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "field_expression") {
        const auto recv = ::ts_node_named_child(node, 0);
        const auto recv_text = node_text(recv, bytes);
        if (cb_names.contains(std::string{recv_text})) {
            out.push_back(node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_field_reads(::ts_node_child(node, i), bytes, cb_names, out);
    }
}

class CbufferLoadInLoop : public Rule {
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
        const auto cb_names = collect_cbuffer_names(bytes);
        if (cb_names.empty())
            return;
        std::vector<::TSNode> reads;
        collect_field_reads(::ts_tree_root_node(tree.raw_tree()), bytes, cb_names, reads);
        for (const auto read : reads) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(read)};
            if (!util::inside_loop(cfg, span))
                continue;
            if (!util::loop_invariant_expr(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span = span;
            diag.message = std::string{
                "cbuffer field read inside a loop -- the value does not change "
                "across iterations; hoisting to a local lets the constant-cache "
                "fetch happen once"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "lift the cbuffer field read to a block-scope local before the "
                "loop -- the compiler may CSE this on its own, but the explicit "
                "hoist removes one constant-cache fetch per iteration"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_cbuffer_load_in_loop() {
    return std::make_unique<CbufferLoadInLoop>();
}

}  // namespace shader_clippy::rules
