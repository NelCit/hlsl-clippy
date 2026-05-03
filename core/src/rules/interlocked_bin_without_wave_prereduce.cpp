// interlocked-bin-without-wave-prereduce
//
// Detects `InterlockedAdd` / `InterlockedOr` calls inside a loop or directly
// against a small fixed-bin set without a preceding `WaveActiveSum` /
// `WavePrefixSum` / `WaveActiveBitOr` reduction. Per-lane atomics serialise
// across the wave; replacing them with one wave-reduce + one
// representative-lane atomic drops 32-64x atomic traffic on every modern IHV.
//
// Stage: Ast. The detection is purely textual on `call_expression` nodes.
// The full uniformity-aware version (which discriminates per-lane vs. wave-
// uniform target slots) is a Phase 4 4c future.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "interlocked-bin-without-wave-prereduce";
constexpr std::string_view k_category = "workgroup";

constexpr std::array<std::string_view, 3> k_atomics{
    "InterlockedAdd",
    "InterlockedOr",
    "InterlockedXor",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        bool is_atomic = false;
        for (const auto name : k_atomics) {
            if (fn_text == name) {
                is_atomic = true;
                break;
            }
        }
        if (is_atomic) {
            // Look for any `WaveActive*` / `WavePrefix*` token in the
            // enclosing function body. Walk up to the nearest function /
            // compound statement and search.
            ::TSNode anc = node;
            while (!::ts_node_is_null(anc)) {
                const auto k = node_kind(anc);
                if (k == "function_definition" || k == "function_declaration") {
                    break;
                }
                anc = ::ts_node_parent(anc);
            }
            if (::ts_node_is_null(anc)) {
                // Fall back to the parent compound statement.
                anc = ::ts_node_parent(node);
            }
            const auto fn_body_text = node_text(anc, bytes);
            const bool has_wave_reduce =
                fn_body_text.find("WaveActiveSum") != std::string_view::npos ||
                fn_body_text.find("WavePrefixSum") != std::string_view::npos ||
                fn_body_text.find("WaveActiveBitOr") != std::string_view::npos ||
                fn_body_text.find("WaveActiveBitXor") != std::string_view::npos;
            if (!has_wave_reduce) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{fn_text} +
                               "() with no preceding WaveActive* / WavePrefix* "
                               "reduction -- per-lane atomics serialise the wave; "
                               "wave-reduce first, then issue one atomic per wave";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "WaveActiveSum / WavePrefixSum the operand, then issue a "
                    "single representative-lane InterlockedAdd; saves 32-64x "
                    "atomic traffic on every modern IHV"};
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

class InterlockedBinWithoutWavePrereduce : public Rule {
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

std::unique_ptr<Rule> make_interlocked_bin_without_wave_prereduce() {
    return std::make_unique<InterlockedBinWithoutWavePrereduce>();
}

}  // namespace shader_clippy::rules
