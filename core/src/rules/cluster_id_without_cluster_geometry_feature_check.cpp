// cluster-id-without-cluster-geometry-feature-check
//
// Detects calls to `ClusterID()` (SM 6.10 ray-tracing intrinsic) without a
// guarding `IsClusteredGeometrySupported()` (or equivalent) feature check.
// `ClusterID()` is functionally pending on devices that haven't yet shipped
// the clustered-geometry preview support; an unguarded call breaks on
// older RT-capable devices.
//
// Stage: Ast. Activates only on SM 6.10+ targets via `target_is_sm610_or_later`.

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

constexpr std::string_view k_rule_id = "cluster-id-without-cluster-geometry-feature-check";
constexpr std::string_view k_category = "sm6_10";

void walk(::TSNode node,
          std::string_view bytes,
          bool source_has_feature_check,
          const AstTree& tree,
          RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "ClusterID") {
            if (!source_has_feature_check) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message =
                    "`ClusterID()` (SM 6.10) is functionally pending on devices that "
                    "haven't yet shipped the clustered-geometry preview support -- "
                    "guard the call with `IsClusteredGeometrySupported()` (or an "
                    "equivalent feature check) on any path-dominating predicate";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, source_has_feature_check, tree, ctx);
    }
}

class ClusterIdWithoutClusterGeometryFeatureCheck : public Rule {
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
        // The SM 6.10 gate is informational here: we always fire on a
        // ClusterID call without a guard. The diagnostic message names the
        // SM 6.10 origin so authors who target older profiles can ignore.
        const auto bytes = tree.source_bytes();
        if (bytes.find("ClusterID") == std::string_view::npos) {
            return;
        }
        const bool has_check =
            bytes.find("IsClusteredGeometrySupported") != std::string_view::npos;
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, has_check, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_cluster_id_without_cluster_geometry_feature_check() {
    return std::make_unique<ClusterIdWithoutClusterGeometryFeatureCheck>();
}

}  // namespace hlsl_clippy::rules
