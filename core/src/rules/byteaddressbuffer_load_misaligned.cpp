// byteaddressbuffer-load-misaligned
//
// Detects `Load2(K)` / `Load3(K)` / `Load4(K)` calls on a `ByteAddressBuffer`
// (or `RWByteAddressBuffer`) where `K` is a compile-time integer literal that
// fails the natural-alignment check for the load width:
//
//   * `Load2`  -> 8-byte natural alignment
//   * `Load3`  -> 16-byte natural alignment (DXC widens 12 to 16 in practice;
//                 conservative check uses 16 to match driver behaviour)
//   * `Load4`  -> 16-byte natural alignment
//
// Under-aligned widened loads either fault outright on some DXR / Vulkan
// drivers or silently split into single-DWORD reads on RDNA / Turing / Ada /
// Xe-HPG -- two-to-four extra memory transactions per load that defeat the
// purpose of using a widened intrinsic in the first place.
//
// Detection strategy:
//   1. Walk the AST for `call_expression` nodes.
//   2. The callee must be a `field_expression` whose `field` text is one of
//      `Load2` / `Load3` / `Load4`, and whose receiver is a plain identifier.
//   3. The first argument must be a `number_literal` whose integer value is
//      not a multiple of the natural alignment for the load width.
//   4. Reflection (Stage::Reflection) confirms the receiver identifier is
//      bound to a ByteAddressBuffer / RWByteAddressBuffer; this keeps the
//      false-positive rate low when a `LoadN` happens to exist on a typed
//      view that doesn't carry the alignment requirement.
//
// The fix is suggestion-only: rounding the offset down (or up) to the next
// natural boundary changes the bytes the shader reads, which is a semantic
// change a human must approve.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

constexpr std::string_view k_rule_id = "byteaddressbuffer-load-misaligned";
constexpr std::string_view k_category = "bindings";

/// Return the natural alignment (in bytes) for a `LoadN` method name. Returns
/// 0 when the name is not one of the targeted methods.
[[nodiscard]] std::uint32_t alignment_for_load(std::string_view method) noexcept {
    if (method == "Load2")
        return 8U;
    if (method == "Load3")
        return 16U;
    if (method == "Load4")
        return 16U;
    return 0U;
}

/// Parse a non-negative integer from a numeric literal. Accepts plain decimal,
/// `0x`-prefixed hex, and a trailing `u`/`U`/`l`/`L` suffix. Returns nullopt
/// if the text is not a non-negative integer the rule can reason about (any
/// non-zero fractional digit, exponent, or sign disqualifies).
[[nodiscard]] std::optional<std::uint64_t> parse_uint_literal(std::string_view text) noexcept {
    if (text.empty())
        return std::nullopt;
    std::size_t i = 0;
    if (text[i] == '+')
        ++i;
    if (i >= text.size())
        return std::nullopt;

    std::uint64_t value = 0U;
    bool any_digit = false;

    if (i + 1U < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2U;
        while (i < text.size()) {
            const char c = text[i];
            std::uint64_t d = 0U;
            if (c >= '0' && c <= '9')
                d = static_cast<std::uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = static_cast<std::uint64_t>(10 + (c - 'a'));
            else if (c >= 'A' && c <= 'F')
                d = static_cast<std::uint64_t>(10 + (c - 'A'));
            else
                break;
            value = (value << 4U) | d;
            any_digit = true;
            ++i;
        }
    } else {
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = (value * 10U) + static_cast<std::uint64_t>(text[i] - '0');
            any_digit = true;
            ++i;
        }
    }

    if (!any_digit)
        return std::nullopt;

    // No fractional part with non-zero digits, no exponent, accept only u/U/l/L
    // suffixes.
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return std::nullopt;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return std::nullopt;
    while (i < text.size()) {
        const char c = text[i];
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L')
            return std::nullopt;
        ++i;
    }

    return value;
}

class ByteAddressBufferLoadMisaligned : public Rule {
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
        // ADR 0020 sub-phase A v1.3.1 — this rule needs an AST to disambiguate
        // the receiver of `.Load*` calls. On `.slang` sources the orchestrator
        // skips tree-sitter parsing (reflection still runs) so the AST is
        // unavailable. Bail silently; the rule re-engages on `.hlsl` and on
        // `.slang` once sub-phase B lands tree-sitter-slang.
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

    static void check_call(::TSNode call,
                           std::string_view bytes,
                           const AstTree& tree,
                           const ReflectionInfo& reflection,
                           RuleContext& ctx) {
        const ::TSNode fn = ::ts_node_child_by_field_name(call, "function", 8);
        if (node_kind(fn) != "field_expression")
            return;

        const ::TSNode receiver = ::ts_node_child_by_field_name(fn, "argument", 8);
        const ::TSNode field = ::ts_node_child_by_field_name(fn, "field", 5);
        if (::ts_node_is_null(receiver) || ::ts_node_is_null(field))
            return;
        if (node_kind(receiver) != "identifier")
            return;

        const auto method_name = node_text(field, bytes);
        const std::uint32_t alignment = alignment_for_load(method_name);
        if (alignment == 0U)
            return;

        const auto receiver_name = node_text(receiver, bytes);
        const auto* binding = util::find_binding_used_by(reflection, receiver_name);
        if (binding == nullptr)
            return;
        if (binding->kind != ResourceKind::ByteAddressBuffer &&
            binding->kind != ResourceKind::RWByteAddressBuffer) {
            return;
        }

        const ::TSNode args = ::ts_node_child_by_field_name(call, "arguments", 9);
        if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) < 1U)
            return;
        const ::TSNode offset_arg = ::ts_node_named_child(args, 0);
        if (node_kind(offset_arg) != "number_literal")
            return;

        const auto literal_text = node_text(offset_arg, bytes);
        const auto value = parse_uint_literal(literal_text);
        if (!value.has_value())
            return;
        if ((*value % static_cast<std::uint64_t>(alignment)) == 0U)
            return;

        const auto call_range = tree.byte_range(call);

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
        diag.message =
            std::string{"`"} + std::string{receiver_name} + "." + std::string{method_name} + "(" +
            std::string{literal_text} + ")` is misaligned -- " + std::string{method_name} +
            " requires a multiple-of-" + std::to_string(static_cast<unsigned>(alignment)) +
            "-byte offset; under-aligned widened loads either fault or "
            "split into single-DWORD reads on RDNA / Turing / Ada / Xe-HPG";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_byteaddressbuffer_load_misaligned() {
    return std::make_unique<ByteAddressBufferLoadMisaligned>();
}

}  // namespace shader_clippy::rules
