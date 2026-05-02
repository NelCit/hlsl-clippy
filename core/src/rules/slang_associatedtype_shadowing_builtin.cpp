// slang-associatedtype-shadowing-builtin
//
// Detects an `associatedtype X;` declaration whose name X collides with
// a built-in HLSL/Slang type (e.g. `associatedtype float`,
// `associatedtype Texture2D`, `associatedtype Buffer`). Subtle bug:
// Slang's name-resolution prefers the associated type over the
// surrounding built-in within the interface scope, so any code
// inside the interface body that references the shadowed name
// silently binds to the abstract associated type rather than the
// concrete built-in.
//
// Stage: Ast. Slang-only.
//
// Empirical AST shape (tree-sitter-slang v1.4.0):
//
//   `associatedtype Differential;` parses as
//   `(associatedtype_declaration (type_identifier))`. The grammar
//   accepts arbitrary identifiers there, including ones that conflict
//   with built-in HLSL/Slang type names. The probe (sub-phase C dev)
//   confirms `associatedtype int;` parses and surfaces `int` as a
//   `type_identifier` child.
//
// The shadow list mirrors HLSL's primitive-type vocabulary +
// the most commonly-used Slang/HLSL resource and texture types. We
// intentionally keep the list short and high-precision; a longer
// list is queued for v1.5.x as more rules consume it.
//
// References:
//   - HLSL specification §"Built-in types".
//   - Slang language guide §"Associated Types".

#include <array>
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

extern "C" {
const ::TSLanguage* tree_sitter_slang(void);
}

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "slang-associatedtype-shadowing-builtin";
constexpr std::string_view k_category = "slang-language";

constexpr std::array k_builtin_type_names = std::to_array<std::string_view>({
    // Scalar primitives.
    "bool", "int", "uint", "float", "double", "half",
    "min16float", "min10float", "min16int", "min12int", "min16uint",
    // Common vector/matrix shorthand families that frequently get
    // shadowed.
    "float2", "float3", "float4", "int2", "int3", "int4",
    "uint2", "uint3", "uint4",
    // Texture + buffer surfaces.
    "Texture1D", "Texture2D", "Texture3D", "TextureCube",
    "Texture2DArray", "TextureCubeArray",
    "RWTexture1D", "RWTexture2D", "RWTexture3D",
    "Buffer", "RWBuffer", "ByteAddressBuffer", "RWByteAddressBuffer",
    "StructuredBuffer", "RWStructuredBuffer", "ConstantBuffer",
    "SamplerState", "SamplerComparisonState",
    "RaytracingAccelerationStructure",
});

[[nodiscard]] bool is_builtin_type_name(std::string_view name) noexcept {
    for (const auto& known : k_builtin_type_names) {
        if (name == known) {
            return true;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "associatedtype_declaration") {
        // First named child is the type_identifier carrying the
        // associated-type name. Walk children until we find one.
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const auto child = ::ts_node_child(node, i);
            if (node_kind(child) != "type_identifier") {
                continue;
            }
            const auto name = node_text(child, bytes);
            if (is_builtin_type_name(name)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                std::string msg;
                msg += "`associatedtype ";
                msg += name;
                msg +=
                    "` shadows a built-in HLSL/Slang type -- inside the "
                    "interface scope, name resolution will prefer this "
                    "associated type over the built-in, silently binding "
                    "to the abstract type. Rename the associated type to "
                    "avoid the collision";
                diag.message = std::move(msg);
                ctx.emit(std::move(diag));
            }
            break;
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SlangAssociatedtypeShadowingBuiltin : public Rule {
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
        if (tree.language() != tree_sitter_slang()) {
            return;
        }
        const auto bytes = tree.source_bytes();
        if (bytes.find("associatedtype") == std::string_view::npos) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_slang_associatedtype_shadowing_builtin() {
    return std::make_unique<SlangAssociatedtypeShadowingBuiltin>();
}

}  // namespace hlsl_clippy::rules
