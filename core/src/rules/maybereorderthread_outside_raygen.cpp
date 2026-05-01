// maybereorderthread-outside-raygen
//
// Detects a `dx::MaybeReorderThread(...)` call inside a function whose
// `[shader("...")]` attribute is not `"raygeneration"`. The SM 6.9 SER
// specification (proposal 0027) restricts `MaybeReorderThread` to the raygen
// stage; reordering must happen before per-lane work that the reorder is
// meant to coalesce.
//
// Stage: Ast (forward-compatible-stub for Reflection-driven stage analysis).
//
// The Slang reflection bridge today does not surface a per-call-site stage
// view (it surfaces per-entry-point stage tags, but `MaybeReorderThread` calls
// can live in helper functions that are reachable from many stages). The
// pragmatic Phase 3 stub matches `[shader("stage")]` attributes preceding a
// function definition and walks the body for `MaybeReorderThread` mentions;
// fires when stage != raygeneration. Once the bridge surfaces call-graph
// stage propagation we should re-implement against `ReflectionInfo`.

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

constexpr std::string_view k_rule_id = "maybereorderthread-outside-raygen";
constexpr std::string_view k_category = "ser";
constexpr std::string_view k_intrinsic = "MaybeReorderThread";

/// Best-effort: does the function source carry a `[shader("...")]` attribute,
/// and if so, what's the stage tag inside the parens?
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
    const auto kind = node_kind(node);
    if (kind == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const auto stage = extract_shader_stage(fn_text);
        // Only fire when we have a definite stage tag and it's not raygen.
        if (!stage.empty() && stage != "raygeneration" &&
            fn_text.find(k_intrinsic) != std::string_view::npos) {
            // Locate the call site within the body for a precise span.
            const auto call_pos = fn_text.find(k_intrinsic);
            const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
            const auto call_lo = node_lo + static_cast<std::uint32_t>(call_pos);
            const auto call_hi =
                call_lo + static_cast<std::uint32_t>(std::string_view{k_intrinsic}.size());

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = call_lo, .hi = call_hi}};
            diag.message =
                std::string{
                    "`dx::MaybeReorderThread` is only allowed in raygeneration shaders "
                    "(SER spec 0027) -- this call lives in a `"} +
                std::string{stage} + "` shader";
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class MaybeReorderThreadOutsideRaygen : public Rule {
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

std::unique_ptr<Rule> make_maybereorderthread_outside_raygen() {
    return std::make_unique<MaybeReorderThreadOutsideRaygen>();
}

}  // namespace hlsl_clippy::rules
