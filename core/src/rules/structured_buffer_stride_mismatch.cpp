// structured-buffer-stride-mismatch
//
// Detects `StructuredBuffer<T>` / `RWStructuredBuffer<T>` declarations whose
// element stride is not a multiple of 16 bytes. Misaligned strides force
// every Nth element to straddle a 16-byte cache-line word on RDNA/Turing/Ada,
// costing an extra memory transaction per misaligned access.
//
// Detection plan: structured-buffer element stride is not a first-class field
// on `ResourceBinding` today. We approximate by mapping the binding's name to
// a `CBufferLayout`-like declaration when the user packs an inline struct via
// `ConstantBuffer<T>`-aliased reflection, otherwise we walk the AST for
// `StructuredBuffer<TypeName>` / `RWStructuredBuffer<TypeName>` declarations
// and look up `TypeName` among the user-defined struct types in the same
// translation unit. For each found struct, sum the natural sizes of the
// declared field types per HLSL packing rules and emit when the total is not
// a multiple of 16. This matches the doc-page detection plan ("uses Slang's
// reflection API to determine `sizeof(T)`") with a tree-sitter-only fallback
// because the reflection bridge does not yet surface `T` for structured
// buffers.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

constexpr std::string_view k_rule_id = "structured-buffer-stride-mismatch";
constexpr std::string_view k_category = "bindings";

/// Best-effort byte size for primitive HLSL scalar / vector / matrix types.
/// Returns 0 for unknown / user-defined types -- caller must skip.
[[nodiscard]] std::uint32_t sizeof_type(std::string_view t) noexcept {
    auto starts_with = [&](std::string_view p) noexcept {
        return t.size() >= p.size() && t.substr(0, p.size()) == p;
    };
    auto suffix_count = [&](std::size_t prefix_len) noexcept -> std::uint32_t {
        if (t.size() <= prefix_len)
            return 1U;
        const char c = t[prefix_len];
        if (c >= '1' && c <= '4')
            return static_cast<std::uint32_t>(c - '0');
        return 1U;
    };
    using Dims = std::pair<std::uint32_t, std::uint32_t>;
    auto matrix_dims = [&](std::size_t prefix_len) noexcept -> Dims {
        if (t.size() < prefix_len + 3U)
            return {1U, 1U};
        const char r = t[prefix_len];
        const char x = t[prefix_len + 1U];
        const char c = t[prefix_len + 2U];
        if ((r >= '1' && r <= '4') && x == 'x' && (c >= '1' && c <= '4')) {
            return {static_cast<std::uint32_t>(r - '0'), static_cast<std::uint32_t>(c - '0')};
        }
        return {1U, 1U};
    };

    // Float / int / uint / bool scalars + vectors.
    if (starts_with("float") && t.find('x') == std::string_view::npos)
        return 4U * suffix_count(5U);
    if (starts_with("int") && !starts_with("int64") && t.find('x') == std::string_view::npos)
        return 4U * suffix_count(3U);
    if (starts_with("uint") && !starts_with("uint64") && t.find('x') == std::string_view::npos)
        return 4U * suffix_count(4U);
    if (starts_with("bool") && t.find('x') == std::string_view::npos)
        return 4U * suffix_count(4U);
    if (starts_with("half") && t.find('x') == std::string_view::npos)
        return 2U * suffix_count(4U);
    if (starts_with("double") && t.find('x') == std::string_view::npos)
        return 8U * suffix_count(6U);

    // Matrices.
    if (starts_with("float")) {
        const auto [r, c] = matrix_dims(5U);
        return 4U * r * c;
    }
    if (starts_with("int")) {
        const auto [r, c] = matrix_dims(3U);
        return 4U * r * c;
    }
    if (starts_with("uint")) {
        const auto [r, c] = matrix_dims(4U);
        return 4U * r * c;
    }

    return 0U;
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

/// Parse the body of `struct Name { ... };` and return the summed size of
/// every field. Returns 0 when any field's type cannot be sized.
[[nodiscard]] std::uint32_t struct_size_from_body(std::string_view body) noexcept {
    std::uint32_t total = 0U;
    std::size_t i = 0U;
    while (i < body.size()) {
        // Skip whitespace.
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' ||
                                   body[i] == '\r' || body[i] == ';'))
            ++i;
        if (i >= body.size())
            break;
        // Read up to the next semicolon as one field declaration.
        const std::size_t start = i;
        while (i < body.size() && body[i] != ';' && body[i] != '}')
            ++i;
        if (i >= body.size())
            break;
        const auto field = trim(body.substr(start, i - start));
        if (field.empty()) {
            ++i;
            continue;
        }
        // Field shape: "<type> <name>[<count>]?"
        std::size_t sp = field.find(' ');
        if (sp == std::string_view::npos) {
            ++i;
            continue;
        }
        const auto type_part = trim(field.substr(0, sp));
        const auto rest = trim(field.substr(sp));
        const std::uint32_t base = sizeof_type(type_part);
        if (base == 0U)
            return 0U;
        // Array size if present: [N]
        std::uint32_t count = 1U;
        const auto lb = rest.find('[');
        const auto rb = rest.find(']');
        if (lb != std::string_view::npos && rb != std::string_view::npos && rb > lb + 1) {
            const auto digits = trim(rest.substr(lb + 1, rb - lb - 1));
            std::uint32_t value = 0U;
            bool ok = !digits.empty();
            for (const char c : digits) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                value = value * 10U + static_cast<std::uint32_t>(c - '0');
            }
            if (ok)
                count = value;
        }
        total += base * count;
        ++i;
    }
    return total;
}

struct StructDef {
    std::uint32_t size_bytes = 0U;
    ByteSpan declaration_span{};
};

void collect_struct_sizes(::TSNode node,
                          std::string_view bytes,
                          std::unordered_map<std::string, StructDef>& out) {
    if (::ts_node_is_null(node))
        return;
    const auto kind = node_kind(node);
    if (kind == "struct_specifier" || kind == "struct_declaration" || kind == "type_definition" ||
        kind == "field_declaration_list") {
        const auto text = node_text(node, bytes);
        // Naive: find `struct <Name> { <body> }` substrings.
        const auto sp = text.find("struct");
        if (sp != std::string_view::npos) {
            std::size_t i = sp + 6U;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
                ++i;
            const std::size_t name_start = i;
            while (i < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                ++i;
            const auto name = text.substr(name_start, i - name_start);
            // Look for `{`.
            const auto lb = text.find('{', i);
            const auto rb = text.rfind('}');
            if (!name.empty() && lb != std::string_view::npos && rb != std::string_view::npos &&
                rb > lb + 1) {
                const auto body = text.substr(lb + 1, rb - lb - 1);
                const auto size = struct_size_from_body(body);
                if (size != 0U) {
                    StructDef def;
                    def.size_bytes = size;
                    const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                    const auto node_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
                    def.declaration_span = ByteSpan{node_lo, node_hi};
                    out.emplace(std::string{name}, def);
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_struct_sizes(::ts_node_child(node, i), bytes, out);
    }
}

void scan_structured_buffer_decls(::TSNode node,
                                  std::string_view bytes,
                                  const std::unordered_map<std::string, StructDef>& structs,
                                  const AstTree& tree,
                                  RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;
    const auto kind = node_kind(node);
    if (kind == "declaration" || kind == "global_variable_declaration" ||
        kind == "variable_declaration" || kind == "field_declaration") {
        const auto text = node_text(node, bytes);
        // Look for `StructuredBuffer<TypeName>` or `RWStructuredBuffer<TypeName>`.
        for (const std::string_view prefix : {"StructuredBuffer<", "RWStructuredBuffer<"}) {
            const auto pos = text.find(prefix);
            if (pos == std::string_view::npos)
                continue;
            const std::size_t name_start = pos + prefix.size();
            const auto end = text.find('>', name_start);
            if (end == std::string_view::npos)
                continue;
            const auto type_name = trim(text.substr(name_start, end - name_start));
            const auto it = structs.find(std::string{type_name});
            if (it == structs.end())
                continue;
            const std::uint32_t size = it->second.size_bytes;
            if (size == 0U || (size % 16U) == 0U)
                continue;
            const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
            const auto node_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{node_lo, node_hi}};
            diag.message = std::string{"structured-buffer element type `"} +
                           std::string{type_name} + "` is " + std::to_string(size) +
                           " bytes (not a multiple of 16) -- pad to a 16-byte boundary to avoid "
                           "cache-line straddling on every Nth element load";
            ctx.emit(std::move(diag));
            break;
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_structured_buffer_decls(::ts_node_child(node, i), bytes, structs, tree, ctx);
    }
}

class StructuredBufferStrideMismatch : public Rule {
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
        std::unordered_map<std::string, StructDef> structs;
        const auto root = ::ts_tree_root_node(tree.raw_tree());
        collect_struct_sizes(root, tree.source_bytes(), structs);
        scan_structured_buffer_decls(root, tree.source_bytes(), structs, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_structured_buffer_stride_mismatch() {
    return std::make_unique<StructuredBufferStrideMismatch>();
}

}  // namespace shader_clippy::rules
