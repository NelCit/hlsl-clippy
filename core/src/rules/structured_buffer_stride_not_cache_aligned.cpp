// structured-buffer-stride-not-cache-aligned
//
// Detects `StructuredBuffer<T>` (or `RWStructuredBuffer<T>`) declarations
// where `sizeof(T)` is a multiple of 4 but not a multiple of the configurable
// cache-line target (default 64 bytes for RDNA; 128 is appropriate for
// Turing/Ada). When the per-element stride straddles a cache line, every
// random read pulls in two lines instead of one, halving useful bandwidth on
// every modern GPU.
//
// Distinct from the locked `structured-buffer-stride-mismatch` rule, which
// targets HLSL packing-rule violations (where the shader-side struct does not
// agree with the host-side struct). This rule fires when the shader and host
// agree but the agreed stride is cache-hostile.
//
// Detection (AST + reflection):
//   1. Reflection identifies all `StructuredBuffer` /
//      `RWStructuredBuffer` / `Append` / `Consume` bindings.
//   2. For each, walk the AST looking for the matching declaration of the
//      form `StructuredBuffer<TypeName> name;` and identify the `TypeName`.
//   3. Look up the `struct TypeName { ... };` declaration in the same
//      translation unit and sum the byte sizes of its primitive-typed
//      fields (`float`, `float2`, `float3`, `float4`, `uint`, `int`,
//      `half`, `min16float`, `double`, plus row/col `float4x4`-style
//      matrices). Conservative: skip the rule if any field has an unknown
//      type, an array dimension, or a nested struct.
//   4. If the computed stride is a non-zero multiple of 4 but not a multiple
//      of the cache-line target, fire.
//
// The fix is suggestion-only: re-laying out the struct involves trade-offs
// (extra padding fields, denormalising AoS into SoA) the linter cannot
// adjudicate.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "structured-buffer-stride-not-cache-aligned";
constexpr std::string_view k_category = "bindings";

// Default cache-line target. RDNA = 64 bytes; Turing/Ada = 128 bytes. We pick
// 64 here because (a) it is the smaller of the two, so a stride that is a
// multiple of 64 is also "not actively bad" for the larger 128-byte line,
// and (b) RDNA is the architecture most commonly bitten by stride straddle
// in practice.
constexpr std::uint32_t k_default_cache_line_bytes = 64U;

/// Map a primitive HLSL type name to its byte size. Returns nullopt for any
/// type the rule does not understand (struct types, arrays, vendor extensions).
/// Conservative on purpose: skipping the rule on an unknown type is safer
/// than guessing wrong.
[[nodiscard]] std::optional<std::uint32_t> primitive_size(std::string_view name) noexcept {
    // 32-bit scalars.
    if (name == "float" || name == "uint" || name == "int" || name == "bool" || name == "dword")
        return 4U;
    // 16-bit scalars.
    if (name == "half" || name == "min16float" || name == "min16uint" || name == "min16int" ||
        name == "uint16_t" || name == "int16_t" || name == "float16_t")
        return 2U;
    // 64-bit scalars.
    if (name == "double" || name == "uint64_t" || name == "int64_t")
        return 8U;
    // Float vectors.
    if (name == "float2")
        return 8U;
    if (name == "float3")
        return 12U;
    if (name == "float4")
        return 16U;
    // Half / min16 vectors.
    if (name == "half2")
        return 4U;
    if (name == "half3")
        return 6U;
    if (name == "half4")
        return 8U;
    // Int / uint vectors.
    if (name == "int2" || name == "uint2")
        return 8U;
    if (name == "int3" || name == "uint3")
        return 12U;
    if (name == "int4" || name == "uint4")
        return 16U;
    // Common matrices (4x4 / 3x3 / 4x3 floats).
    if (name == "float4x4" || name == "row_major float4x4" || name == "column_major float4x4")
        return 64U;
    if (name == "float3x3")
        return 36U;
    if (name == "float4x3" || name == "float3x4")
        return 48U;
    return std::nullopt;
}

/// Extract the type-parameter text from a `StructuredBuffer<T>` /
/// `RWStructuredBuffer<T>` declaration's type node. Returns empty when the
/// declaration text doesn't fit the simple `Container<TypeName>` shape.
[[nodiscard]] std::string_view extract_type_parameter(std::string_view decl_text) noexcept {
    const auto lt = decl_text.find('<');
    const auto gt = decl_text.rfind('>');
    if (lt == std::string_view::npos || gt == std::string_view::npos || gt <= lt + 1U)
        return {};
    auto inner = decl_text.substr(lt + 1U, gt - lt - 1U);
    // Trim leading / trailing whitespace.
    while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t'))
        inner.remove_prefix(1U);
    while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t'))
        inner.remove_suffix(1U);
    return inner;
}

/// Find a `struct_specifier` whose name matches `target` anywhere in the tree
/// rooted at `root`. Returns a null TSNode on miss.
[[nodiscard]] ::TSNode find_struct(::TSNode root,
                                   std::string_view bytes,
                                   std::string_view target) noexcept {
    if (::ts_node_is_null(root))
        return root;
    const auto kind = node_kind(root);
    if (kind == "struct_specifier") {
        const ::TSNode name_node = ::ts_node_child_by_field_name(root, "name", 4);
        if (!::ts_node_is_null(name_node) && node_text(name_node, bytes) == target) {
            return root;
        }
    }
    const std::uint32_t count = ::ts_node_child_count(root);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto child = ::ts_node_child(root, i);
        const auto found = find_struct(child, bytes, target);
        if (!::ts_node_is_null(found))
            return found;
    }
    ::TSNode null_node{};
    return null_node;
}

/// Sum the sizes of every field declared in `struct_node`'s body. Returns
/// nullopt when the struct contains a field whose type is not in our
/// primitive-size table, or when the struct body cannot be located.
[[nodiscard]] std::optional<std::uint32_t> struct_size(::TSNode struct_node,
                                                       std::string_view bytes) noexcept {
    if (::ts_node_is_null(struct_node))
        return std::nullopt;
    // The body is a `field_declaration_list` child.
    ::TSNode body{};
    {
        const std::uint32_t count = ::ts_node_child_count(struct_node);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto child = ::ts_node_child(struct_node, i);
            if (node_kind(child) == "field_declaration_list") {
                body = child;
                break;
            }
        }
    }
    if (::ts_node_is_null(body))
        return std::nullopt;

    std::uint32_t total = 0U;
    const std::uint32_t named_count = ::ts_node_named_child_count(body);
    for (std::uint32_t i = 0; i < named_count; ++i) {
        const ::TSNode field = ::ts_node_named_child(body, i);
        if (node_kind(field) != "field_declaration")
            continue;
        const ::TSNode type_node = ::ts_node_child_by_field_name(field, "type", 4);
        if (::ts_node_is_null(type_node))
            return std::nullopt;
        // Reject array-typed fields (declarator carries the array dimension).
        // We detect arrays by checking the declarator subtree for `[`.
        const ::TSNode declarator = ::ts_node_child_by_field_name(field, "declarator", 10);
        if (!::ts_node_is_null(declarator)) {
            const auto decl_text = node_text(declarator, bytes);
            if (decl_text.find('[') != std::string_view::npos)
                return std::nullopt;
        }
        const auto type_text = node_text(type_node, bytes);
        const auto sz = primitive_size(type_text);
        if (!sz.has_value())
            return std::nullopt;
        total += *sz;
    }
    return total == 0U ? std::nullopt : std::optional<std::uint32_t>{total};
}

class StructuredBufferStrideNotCacheAligned : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());

        for (const auto& binding : reflection.bindings) {
            if (binding.kind != ResourceKind::StructuredBuffer &&
                binding.kind != ResourceKind::RWStructuredBuffer &&
                binding.kind != ResourceKind::AppendStructuredBuffer &&
                binding.kind != ResourceKind::ConsumeStructuredBuffer) {
                continue;
            }
            check_binding(binding, root, bytes, tree, ctx);
        }
    }

private:
    static void check_binding(const ResourceBinding& binding,
                              ::TSNode root,
                              std::string_view bytes,
                              const AstTree& tree,
                              RuleContext& ctx) {
        // Find the AST declaration whose declarator-name matches binding.name.
        ::TSNode decl_node = find_declaration(root, bytes, binding.name);
        if (::ts_node_is_null(decl_node))
            return;

        const ::TSNode type_node = ::ts_node_child_by_field_name(decl_node, "type", 4);
        if (::ts_node_is_null(type_node))
            return;
        const auto type_text = node_text(type_node, bytes);
        const auto type_param = extract_type_parameter(type_text);
        if (type_param.empty())
            return;

        const ::TSNode struct_node = find_struct(root, bytes, type_param);
        if (::ts_node_is_null(struct_node))
            return;

        const auto stride = struct_size(struct_node, bytes);
        if (!stride.has_value())
            return;
        if (*stride == 0U)
            return;
        if ((*stride % 4U) != 0U)
            return;
        if ((*stride % k_default_cache_line_bytes) == 0U)
            return;

        const auto decl_range = tree.byte_range(decl_node);

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = decl_range};
        diag.message =
            std::string{"`"} + binding.name + "` has element type `" + std::string{type_param} +
            "` with stride " + std::to_string(static_cast<unsigned>(*stride)) + " bytes -- not a " +
            "multiple of the " + std::to_string(static_cast<unsigned>(k_default_cache_line_bytes)) +
            "-byte cache line, so every random read straddles two lines on RDNA / Turing / Ada";
        ctx.emit(std::move(diag));
    }

    /// Walk the tree looking for a top-level `declaration` whose declarator
    /// name matches `target`. Returns a null node on miss.
    static ::TSNode find_declaration(::TSNode root,
                                     std::string_view bytes,
                                     std::string_view target) noexcept {
        if (::ts_node_is_null(root)) {
            ::TSNode null_node{};
            return null_node;
        }
        const auto kind = node_kind(root);
        if (kind == "declaration") {
            const ::TSNode declarator = ::ts_node_child_by_field_name(root, "declarator", 10);
            if (!::ts_node_is_null(declarator)) {
                // The declarator may be an `init_declarator` wrapping an
                // `identifier`, or a bare identifier.
                ::TSNode name_node = declarator;
                if (node_kind(declarator) == "init_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(declarator, "declarator", 10);
                    if (!::ts_node_is_null(inner))
                        name_node = inner;
                }
                if (node_text(name_node, bytes) == target)
                    return root;
            }
        }
        const std::uint32_t count = ::ts_node_child_count(root);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto child = ::ts_node_child(root, i);
            const auto found = find_declaration(child, bytes, target);
            if (!::ts_node_is_null(found))
                return found;
        }
        ::TSNode null_node{};
        return null_node;
    }
};

}  // namespace

std::unique_ptr<Rule> make_structured_buffer_stride_not_cache_aligned() {
    return std::make_unique<StructuredBufferStrideNotCacheAligned>();
}

}  // namespace hlsl_clippy::rules
