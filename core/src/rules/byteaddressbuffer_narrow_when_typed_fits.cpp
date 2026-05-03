// byteaddressbuffer-narrow-when-typed-fits
//
// Detects the `asfloat(buf.Load4(K))` (or `asfloat4(...)`) pattern on a
// `ByteAddressBuffer` binding. The shader is using a ByteAddressBuffer view
// to read 16 bytes that are already a `float4` -- a typed view
// (`Buffer<float4>` or `StructuredBuffer<float4>`) would deliver the same
// bytes through the texture cache (RDNA) or L1 (Turing/Ada), which is the
// faster path for the access pattern that motivates a widened load.
//
// Detection (AST + reflection):
//   1. `call_expression` whose function identifier is `asfloat` / `asfloat2`
//      / `asfloat3` / `asfloat4` / `asuint` / `asuint2` / `asuint3` /
//      `asuint4` / `asint*`.
//   2. The single argument is itself a `call_expression` whose function is a
//      `field_expression` (`buf.Load4(...)` shape) where the field text is
//      one of `Load`, `Load2`, `Load3`, `Load4`.
//   3. The receiver of the inner call is a plain identifier; reflection
//      confirms the identifier is bound to a ByteAddressBuffer /
//      RWByteAddressBuffer.
//
// The fix is suggestion-only: the rewrite to a typed view requires the
// developer to update the resource binding on the host side, which the
// linter cannot do.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_resource.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "byteaddressbuffer-narrow-when-typed-fits";
constexpr std::string_view k_category = "bindings";

/// True when `name` is one of the bit-cast intrinsics that is commonly used
/// to reinterpret raw `uint`/`uint2`/`uint3`/`uint4` returns from a
/// ByteAddressBuffer Load* into a typed scalar / vector.
[[nodiscard]] bool is_bitcast_intrinsic(std::string_view name) noexcept {
    return name == "asfloat" || name == "asfloat2" || name == "asfloat3" || name == "asfloat4" ||
           name == "asuint" || name == "asuint2" || name == "asuint3" || name == "asuint4" ||
           name == "asint" || name == "asint2" || name == "asint3" || name == "asint4" ||
           name == "asdouble";
}

/// True when `name` is one of the LoadN methods on a ByteAddressBuffer that
/// returns a typed-fittable POD (`Load` returns one DWORD, `Load4` returns
/// four). All four widths benefit from the typed-view rewrite if the typed
/// view exists.
[[nodiscard]] bool is_load_method(std::string_view name) noexcept {
    return name == "Load" || name == "Load2" || name == "Load3" || name == "Load4";
}

class ByteAddressBufferNarrowWhenTypedFits : public Rule {
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
        // ADR 0020 sub-phase A v1.3.1 — needs the AST to inspect call-site
        // typed-load shapes. Bail silently when no tree is available
        // (`.slang` until sub-phase B).
        if (tree.raw_tree() == nullptr) {
            return;
        }
        const auto bytes = tree.source_bytes();
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, reflection, ctx);
    }

private:
    static void walk(::TSNode node,
                     std::string_view bytes,
                     const AstTree& tree,
                     const ReflectionInfo& reflection,
                     RuleContext& ctx) {
        if (::ts_node_is_null(node))
            return;

        if (node_kind(node) == "call_expression") {
            check_call(node, bytes, tree, reflection, ctx);
        }

        const std::uint32_t count = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < count; ++i) {
            walk(::ts_node_child(node, i), bytes, tree, reflection, ctx);
        }
    }

    static void check_call(::TSNode outer_call,
                           std::string_view bytes,
                           const AstTree& tree,
                           const ReflectionInfo& reflection,
                           RuleContext& ctx) {
        // Outer call must be a bit-cast intrinsic.
        const ::TSNode outer_fn = ::ts_node_child_by_field_name(outer_call, "function", 8);
        if (node_kind(outer_fn) != "identifier")
            return;
        if (!is_bitcast_intrinsic(node_text(outer_fn, bytes)))
            return;

        const ::TSNode outer_args = ::ts_node_child_by_field_name(outer_call, "arguments", 9);
        if (::ts_node_is_null(outer_args) || ::ts_node_named_child_count(outer_args) != 1U)
            return;

        // Inner call must be `<receiver>.Load*(...)`.
        const ::TSNode inner_call = ::ts_node_named_child(outer_args, 0);
        if (node_kind(inner_call) != "call_expression")
            return;

        const ::TSNode inner_fn = ::ts_node_child_by_field_name(inner_call, "function", 8);
        if (node_kind(inner_fn) != "field_expression")
            return;

        const ::TSNode receiver = ::ts_node_child_by_field_name(inner_fn, "argument", 8);
        const ::TSNode field = ::ts_node_child_by_field_name(inner_fn, "field", 5);
        if (node_kind(receiver) != "identifier" || ::ts_node_is_null(field))
            return;

        const auto method_name = node_text(field, bytes);
        if (!is_load_method(method_name))
            return;

        const auto receiver_name = node_text(receiver, bytes);
        const auto* binding = util::find_binding_used_by(reflection, receiver_name);
        if (binding == nullptr)
            return;
        if (binding->kind != ResourceKind::ByteAddressBuffer &&
            binding->kind != ResourceKind::RWByteAddressBuffer) {
            return;
        }

        const auto outer_range = tree.byte_range(outer_call);

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = outer_range};
        diag.message = std::string{"`"} + std::string{receiver_name} + "." +
                       std::string{method_name} +
                       "(...)` reinterpreted via a bit-cast is reading typed POD "
                       "through a ByteAddressBuffer -- a typed view "
                       "(`Buffer<float4>` / `StructuredBuffer<T>`) goes through "
                       "the texture cache on RDNA and L1 on Turing/Ada, which is "
                       "usually the faster path for this access pattern";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_byteaddressbuffer_narrow_when_typed_fits() {
    return std::make_unique<ByteAddressBufferNarrowWhenTypedFits>();
}

}  // namespace shader_clippy::rules
