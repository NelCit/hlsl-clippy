// triangle-object-positions-without-allow-data-access-flag
//
// SM 6.10 ray-tracing intrinsic `TriangleObjectPositions()` requires that
// the underlying acceleration structure was built with the
// `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` /
// `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR` flag at BLAS
// build time. Calling without the flag is undefined behaviour. The flag
// is project-side state; we cannot inspect it from shader source. The
// rule is suggestion-grade and flags every `TriangleObjectPositions()`
// call site with a docs link so authors can verify their BLAS build.
//
// Stage: Ast.

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

constexpr std::string_view k_rule_id = "triangle-object-positions-without-allow-data-access-flag";
constexpr std::string_view k_category = "dxr";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "TriangleObjectPositions") {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message =
                "(suggestion) `TriangleObjectPositions()` (SM 6.10) requires the BLAS "
                "to be built with `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` "
                "(D3D12) or `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR` "
                "(Vulkan) -- without it the call is undefined behaviour. Verify your "
                "BLAS-build CPU-side code sets the flag";
            ctx.emit(std::move(diag));
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class TriangleObjectPositionsWithoutAllowDataAccessFlag : public Rule {
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

std::unique_ptr<Rule> make_triangle_object_positions_without_allow_data_access_flag() {
    return std::make_unique<TriangleObjectPositionsWithoutAllowDataAccessFlag>();
}

}  // namespace hlsl_clippy::rules
