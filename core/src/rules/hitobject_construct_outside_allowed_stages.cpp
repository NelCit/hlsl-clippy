// hitobject-construct-outside-allowed-stages
//
// Detects a `dx::HitObject::TraceRay`, `dx::HitObject::FromRayQuery`, or
// `dx::HitObject::MakeMiss` constructor call in a shader stage that is not in
// the SER spec's allowed-stages set (raygeneration, closesthit, miss, with
// stage-specific restrictions per HLSL proposal 0027).
//
// Stage: Ast (forward-compatible-stub for Reflection-driven stage analysis).
//
// Like `maybereorderthread-outside-raygen`, the Slang reflection bridge does
// not yet plumb call-graph stage propagation, so this rule walks the AST,
// extracts the stage tag from each function's `[shader("...")]` attribute,
// and fires when a body mentions one of the constructor names while the
// stage is not in {raygeneration, closesthit, miss}.

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

constexpr std::string_view k_rule_id = "hitobject-construct-outside-allowed-stages";
constexpr std::string_view k_category = "ser";

constexpr std::array<std::string_view, 3> k_constructors{
    "HitObject::TraceRay",
    "HitObject::FromRayQuery",
    "HitObject::MakeMiss",
};

constexpr std::array<std::string_view, 3> k_allowed_stages{
    "raygeneration",
    "closesthit",
    "miss",
};

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

[[nodiscard]] bool is_allowed_stage(std::string_view stage) noexcept {
    for (const auto allowed : k_allowed_stages) {
        if (allowed == stage) {
            return true;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const auto stage = extract_shader_stage(fn_text);
        if (!stage.empty() && !is_allowed_stage(stage)) {
            for (const auto ctor : k_constructors) {
                const auto pos = fn_text.find(ctor);
                if (pos == std::string_view::npos) {
                    continue;
                }
                const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                const auto call_lo = node_lo + static_cast<std::uint32_t>(pos);
                const auto call_hi = call_lo + static_cast<std::uint32_t>(ctor.size());

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{.lo = call_lo, .hi = call_hi}};
                diag.message = std::string{"`dx::"} + std::string{ctor} +
                               "` is not allowed in a `" + std::string{stage} +
                               "` shader; SER spec 0027 restricts HitObject "
                               "construction to raygeneration / closesthit / miss";
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class HitObjectConstructOutsideAllowedStages : public Rule {
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

std::unique_ptr<Rule> make_hitobject_construct_outside_allowed_stages() {
    return std::make_unique<HitObjectConstructOutsideAllowedStages>();
}

}  // namespace shader_clippy::rules
