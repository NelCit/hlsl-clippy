// oriented-bbox-not-set-on-rdna4
//
// Defensive rule for the project-side BLAS-build flag
// `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX`. RDNA 4
// gains up to 10% RT perf when the flag is set; we cannot inspect the
// flag from shader source. The rule fires once per source compiled on
// RDNA-targeted experimental builds, with a docs link.
//
// Stage: Reflection. Gated behind `[experimental.target = rdna4]`.
// Suggestion-grade.

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

constexpr std::string_view k_rule_id = "oriented-bbox-not-set-on-rdna4";
constexpr std::string_view k_category = "rdna4";

[[nodiscard]] bool source_has_rt_call(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return false;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "TraceRay" || fn_text == "TraceRayInline") {
            return true;
        }
        // RayQuery::Proceed style.
        if (node_kind(fn) == "field_expression") {
            const auto field = ::ts_node_child_by_field_name(fn, "field", 5);
            const auto fname = node_text(field, bytes);
            if (fname == "Proceed" || fname == "TraceRayInline") {
                return true;
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        if (source_has_rt_call(::ts_node_child(node, i), bytes)) {
            return true;
        }
    }
    return false;
}

class OrientedBboxNotSetOnRdna4 : public Rule {
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
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept override {
        return ExperimentalTarget::Rdna4;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto root = ::ts_tree_root_node(tree.raw_tree());
        if (!source_has_rt_call(root, tree.source_bytes())) {
            return;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Note;
        diag.primary_span = Span{
            .source = tree.source_id(),
            .bytes = ByteSpan{0U, 0U},
        };
        diag.message =
            "(suggestion) source uses ray-tracing on an RDNA 4 experimental target -- "
            "verify the project-side BLAS build sets "
            "`D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` (or the "
            "equivalent VK Oriented-BBox flag) for up to 10% RT perf on RX 9070";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_oriented_bbox_not_set_on_rdna4() {
    return std::make_unique<OrientedBboxNotSetOnRdna4>();
}

}  // namespace shader_clippy::rules
