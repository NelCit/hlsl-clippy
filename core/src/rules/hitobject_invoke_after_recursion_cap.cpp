// hitobject-invoke-after-recursion-cap
//
// Detects a `dx::HitObject::Invoke(...)` call reachable from a closesthit
// shader chain whose nominal recursion depth would exceed the pipeline's
// `MaxTraceRecursionDepth`.
//
// Stage: Ast (forward-compatible-stub for Phase 4 call-graph + recursion
// analysis).
//
// The full rule needs a project-level call graph + recursion-budget tracker
// + reflection-surfaced pipeline `MaxTraceRecursionDepth` value. None of
// those are wired today (the Slang bridge does not surface ray-pipeline
// subobjects, and `ControlFlowInfo` does not yet do call-graph tracing
// across raygen / closesthit boundaries). This Phase 3 stub fires on the
// pre-flight pattern: a `[shader("closesthit")]` function that calls
// `.Invoke(` on a HitObject value -- the most common direct trigger noted in
// proposal 0027. A precise depth budget will land in Phase 4.

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

constexpr std::string_view k_rule_id = "hitobject-invoke-after-recursion-cap";
constexpr std::string_view k_category = "ser";

[[nodiscard]] std::string_view extract_shader_stage(std::string_view fn_text) noexcept {
    const auto attr = fn_text.find("shader(");
    if (attr == std::string_view::npos) {
        return {};
    }
    const auto open_q = fn_text.find('"', attr);
    if (open_q == std::string_view::npos) {
        return {};
    }
    const auto close_q = fn_text.find('"', open_q + 1);
    if (close_q == std::string_view::npos) {
        return {};
    }
    return fn_text.substr(open_q + 1, close_q - open_q - 1);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const auto stage = extract_shader_stage(fn_text);
        if (stage == "closesthit") {
            // Look for `.Invoke(` -- chained call on a HitObject value.
            const auto invoke_pos = fn_text.find(".Invoke(");
            // And require a HitObject mention nearby (pre-flight; the Phase 4
            // rule will track types properly).
            if (invoke_pos != std::string_view::npos &&
                fn_text.find("HitObject") != std::string_view::npos) {
                const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                const auto call_lo = node_lo + static_cast<std::uint32_t>(invoke_pos + 1);
                const auto call_hi =
                    call_lo + static_cast<std::uint32_t>(std::string_view{"Invoke"}.size());

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{.lo = call_lo, .hi = call_hi}};
                diag.message = std::string{
                    "`HitObject::Invoke` from a closesthit shader consumes a "
                    "trace-recursion-depth slot; verify the pipeline's "
                    "`MaxTraceRecursionDepth` covers this chain (SER spec 0027). "
                    "Phase 4 will track the depth budget exactly"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class HitObjectInvokeAfterRecursionCap : public Rule {
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

std::unique_ptr<Rule> make_hitobject_invoke_after_recursion_cap() {
    return std::make_unique<HitObjectInvokeAfterRecursionCap>();
}

}  // namespace shader_clippy::rules
