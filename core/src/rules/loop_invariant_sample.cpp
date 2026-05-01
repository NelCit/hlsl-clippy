// loop-invariant-sample
//
// Detects `Texture.Sample*` calls inside a loop where the UV / location
// argument is loop-invariant. The sample produces the same result on every
// iteration; hoisting it out of the loop saves the texture-fetch cost
// per iteration.
//
// Stage: ControlFlow. Uses `cfg_query::inside_loop` and
// `light_dataflow::loop_invariant_expr` to gate the firing.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/light_dataflow.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "loop-invariant-sample";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 5> k_sample_names{
    "Sample",
    "SampleLevel",
    "SampleGrad",
    "SampleBias",
    "SampleCmp",
};

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

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    if (keyword.empty())
        return false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// Walk up to the nearest enclosing `for_statement` / `while_statement` /
/// `do_statement`. Returns a null node if none.
[[nodiscard]] ::TSNode enclosing_loop(::TSNode node) noexcept {
    auto n = ::ts_node_parent(node);
    while (!::ts_node_is_null(n)) {
        const auto k = node_kind(n);
        if (k == "for_statement" || k == "while_statement" || k == "do_statement") {
            return n;
        }
        n = ::ts_node_parent(n);
    }
    return n;
}

/// Collect identifier names declared inside the loop init / body. Walks
/// every `init_declarator` under `loop` and emits the inner identifier
/// name. Cheap textual approximation; falls back to scanning the text of
/// declarations when fields are missing.
void collect_loop_local_idents(::TSNode loop,
                               std::string_view bytes,
                               std::vector<std::string>& out) {
    if (::ts_node_is_null(loop))
        return;
    std::vector<::TSNode> stack;
    stack.push_back(loop);
    while (!stack.empty()) {
        const auto n = stack.back();
        stack.pop_back();
        const auto k = node_kind(n);
        if (k == "init_declarator" || k == "declaration") {
            // The first identifier-typed named child is the declared name.
            const std::uint32_t cnt = ::ts_node_named_child_count(n);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_named_child(n, i);
                if (node_kind(child) == "identifier") {
                    const auto name = node_text(child, bytes);
                    if (!name.empty()) {
                        out.emplace_back(name);
                    }
                    break;
                }
            }
        }
        const std::uint32_t cnt = ::ts_node_child_count(n);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const auto child = ::ts_node_child(n, i);
            if (!::ts_node_is_null(child)) {
                stack.push_back(child);
            }
        }
    }
}

void collect_sample_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        // Match `<recv>.Sample*` patterns by checking the suffix.
        for (const auto name : k_sample_names) {
            const auto pos = fn_text.rfind(name);
            if (pos != std::string_view::npos && pos + name.size() == fn_text.size() && pos > 0 &&
                fn_text[pos - 1] == '.') {
                out.push_back(node);
                break;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_sample_calls(::ts_node_child(node, i), bytes, out);
    }
}

class LoopInvariantSample : public Rule {
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
        std::vector<::TSNode> sample_calls;
        collect_sample_calls(
            ::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), sample_calls);
        const auto bytes = tree.source_bytes();
        for (const auto call : sample_calls) {
            const auto call_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_loop(cfg, call_span))
                continue;
            // Inspect the second positional argument (the UV / location); the
            // first is the sampler. If the UV expression is loop-invariant,
            // the whole sample is.
            const auto args = ::ts_node_child_by_field_name(call, "arguments", 9);
            if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) < 2U)
                continue;
            const auto uv = ::ts_node_named_child(args, 1);
            const auto uv_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(uv)};
            if (!util::loop_invariant_expr(cfg, uv_span))
                continue;

            // The uniformity oracle currently classifies loop-counter
            // identifiers as `Uniform` (per ADR 0013 sub-phase 4a's
            // best-effort contract — loop-counter detection is a future
            // tightening). Defend against that by scanning the UV
            // expression's text for any identifier declared inside the
            // enclosing loop. If the UV references such an identifier,
            // it is loop-iteration-varying and we skip the diagnostic.
            const auto loop_node = enclosing_loop(call);
            if (!::ts_node_is_null(loop_node)) {
                std::vector<std::string> loop_locals;
                collect_loop_local_idents(loop_node, bytes, loop_locals);
                const auto uv_text = node_text(uv, bytes);
                bool uv_uses_loop_local = false;
                for (const auto& name : loop_locals) {
                    if (has_keyword(uv_text, name)) {
                        uv_uses_loop_local = true;
                        break;
                    }
                }
                if (uv_uses_loop_local) {
                    continue;
                }
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = call_span;
            diag.message = std::string{
                "texture sample inside a loop with loop-invariant UV -- the "
                "fetch returns the same value on every iteration; hoist it "
                "out of the loop"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "lift the sample to a block-scope local before the loop and "
                "reuse it inside; saves one texture fetch per iteration on "
                "every IHV"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_loop_invariant_sample() {
    return std::make_unique<LoopInvariantSample>();
}

}  // namespace hlsl_clippy::rules
