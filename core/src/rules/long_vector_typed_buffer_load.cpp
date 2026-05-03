// long-vector-typed-buffer-load
//
// Detects a `Buffer<vector<T, N>>` / `RWBuffer<vector<T, N>>` declaration
// with `N >= 5`. Typed buffers go through the texture cache, which has no
// DXGI format equivalent for long vectors -- the SM 6.9 long-vector spec
// requires `ByteAddressBuffer` / `StructuredBuffer` for long-vector data.
//
// Stage: Ast (forward-compatible-stub for Reflection-driven binding-kind
// classification).
//
// `ReflectionInfo::ResourceBinding` exposes a `ResourceKind::Buffer` /
// `RWBuffer` enum, but does not surface the templated element type. The
// Phase 3 stub walks declaration nodes and matches the source pattern
// `Buffer< ... vector<..., N>... >` with `N >= 5`. Once the bridge surfaces
// element type information we can match against `ReflectionInfo` directly.

#include <array>
#include <cctype>
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

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "long-vector-typed-buffer-load";
constexpr std::string_view k_category = "long-vectors";

[[nodiscard]] bool mentions_long_vector(std::string_view text) noexcept {
    auto pos = text.find("vector<");
    while (pos != std::string_view::npos) {
        const auto comma = text.find(',', pos);
        const auto close = text.find('>', pos);
        if (comma != std::string_view::npos && close != std::string_view::npos && comma < close) {
            auto p = comma + 1U;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
                ++p;
            }
            std::uint32_t n = 0;
            bool any = false;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
                n = n * 10U + static_cast<std::uint32_t>(text[p] - '0');
                any = true;
                ++p;
            }
            if (any && n >= 5U) {
                return true;
            }
        }
        pos = text.find("vector<", pos + 1U);
    }
    constexpr std::array<std::string_view, 6> k_prefixes{
        "float",
        "int",
        "uint",
        "half",
        "bool",
        "double",
    };
    for (const auto prefix : k_prefixes) {
        std::size_t p = 0;
        while (p < text.size()) {
            const auto found = text.find(prefix, p);
            if (found == std::string_view::npos) {
                break;
            }
            const bool ok_left = (found == 0) || !is_id_char(text[found - 1]);
            if (ok_left) {
                std::size_t q = found + prefix.size();
                std::uint32_t n = 0;
                bool any = false;
                while (q < text.size() && text[q] >= '0' && text[q] <= '9') {
                    n = n * 10U + static_cast<std::uint32_t>(text[q] - '0');
                    any = true;
                    ++q;
                }
                const bool ok_right = (q >= text.size()) || !is_id_char(text[q]);
                if (any && ok_right && n >= 5U) {
                    return true;
                }
            }
            p = found + 1U;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    if (kind == "declaration" || kind == "global_variable_declaration" ||
        kind == "field_declaration") {
        const auto text = node_text(node, bytes);
        const bool is_typed_buffer = text.find("Buffer<") != std::string_view::npos ||
                                     text.find("RWBuffer<") != std::string_view::npos;
        // Exclude ByteAddressBuffer / StructuredBuffer specifically.
        const bool is_excluded = text.find("ByteAddressBuffer") != std::string_view::npos ||
                                 text.find("StructuredBuffer") != std::string_view::npos ||
                                 text.find("AppendStructuredBuffer") != std::string_view::npos ||
                                 text.find("ConsumeStructuredBuffer") != std::string_view::npos;
        if (is_typed_buffer && !is_excluded && mentions_long_vector(text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message =
                std::string{"typed Buffer / RWBuffer cannot hold a long vector (N >= 5); " +
                            std::string{"SM 6.9 long-vector spec requires `ByteAddressBuffer` "} +
                            "or `StructuredBuffer` for long-vector data"};
            ctx.emit(std::move(diag));
            return;
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LongVectorTypedBufferLoad : public Rule {
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

std::unique_ptr<Rule> make_long_vector_typed_buffer_load() {
    return std::make_unique<LongVectorTypedBufferLoad>();
}

}  // namespace shader_clippy::rules
