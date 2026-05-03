// omm-traceray-force-omm-2state-without-pipeline-flag
//
// Detects a `TraceRay(...)` call with `RAY_FLAG_FORCE_OMM_2_STATE` set when
// the DXR pipeline subobject's
// `D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS` is not set.
//
// Stage: Ast (forward-compatible-stub for Reflection-driven pipeline-flag
// inspection).
//
// The Slang reflection bridge does not yet surface DXR pipeline subobjects,
// so the rule cannot read the project-level allow flag. The Phase 3 stub
// fires on the per-trace force flag without the per-trace allow flag (the
// closely related DXR 1.2 form -- if both are missing on the trace itself,
// the pipeline almost certainly also lacks the allow flag). Once reflection
// surfaces pipeline-config1 we can replace the stub with a precise
// project-level check.

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

constexpr std::string_view k_rule_id = "omm-traceray-force-omm-2state-without-pipeline-flag";
constexpr std::string_view k_category = "opacity-micromaps";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        const auto open = call_text.find('(');
        if (open != std::string_view::npos) {
            // Crude function-name extraction.
            std::size_t hi = open;
            while (hi > 0 && (call_text[hi - 1] == ' ' || call_text[hi - 1] == '\t')) {
                --hi;
            }
            std::size_t lo = hi;
            while (lo > 0) {
                const char c = call_text[lo - 1];
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '_';
                if (!ok) {
                    break;
                }
                --lo;
            }
            const auto name = call_text.substr(lo, hi - lo);
            if (name == "TraceRay") {
                const bool has_force =
                    call_text.find("RAY_FLAG_FORCE_OMM_2_STATE") != std::string_view::npos;
                if (has_force) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Error;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "`TraceRay` with `RAY_FLAG_FORCE_OMM_2_STATE` is "
                        "undefined behaviour unless the DXR pipeline subobject "
                        "(application-side) sets "
                        "`D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS`. "
                        "Verify the C++ pipeline-config1 flags"};
                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class OmmTraceRayForceOmm2StateWithoutPipelineFlag : public Rule {
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

std::unique_ptr<Rule> make_omm_traceray_force_omm_2state_without_pipeline_flag() {
    return std::make_unique<OmmTraceRayForceOmm2StateWithoutPipelineFlag>();
}

}  // namespace shader_clippy::rules
