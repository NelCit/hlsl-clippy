// long-vector-in-cbuffer-or-signature
//
// Detects a `vector<T, N>` (with `N >= 5`) declared as a member of a
// `cbuffer` / `ConstantBuffer<T>` block, or as a struct member that names a
// graphics IO semantic (TEXCOORD / SV_Target / etc.).
//
// Stage: Ast (forward-compatible-stub for Reflection-driven boundary
// classification).
//
// The Slang reflection bridge surfaces cbuffer layouts but does not yet
// surface stage-IO signatures. The Phase 3 stub walks `cbuffer ... { ... }`
// blocks for long-vector member declarations, and walks struct definitions
// looking for `: TEXCOORD<N>` / `: SV_Target` semantics on long-vector
// fields. Once the bridge surfaces a `SignatureElement` view, we can replace
// the stub with a clean reflection-driven check.

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

constexpr std::string_view k_rule_id = "long-vector-in-cbuffer-or-signature";
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

[[nodiscard]] bool mentions_io_semantic(std::string_view text) noexcept {
    return text.find("TEXCOORD") != std::string_view::npos ||
           text.find("SV_Target") != std::string_view::npos ||
           text.find("SV_Position") != std::string_view::npos ||
           text.find("COLOR") != std::string_view::npos ||
           text.find("NORMAL") != std::string_view::npos ||
           text.find("TANGENT") != std::string_view::npos ||
           text.find("BINORMAL") != std::string_view::npos;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    // tree-sitter-hlsl emits `cbuffer_declaration` for `cbuffer X { ... }` in
    // some shapes, but tree-sitter-cpp commonly mis-parses
    // `cbuffer Frame { ... }` as a `function_definition` (cbuffer is the
    // return type). Also tolerate `declaration` / `global_variable_declaration`
    // and `function_definition` so the textual `cbuffer` / `ConstantBuffer<`
    // check covers every grammar shape we have observed in the wild. See
    // external/treesitter-version.md for the cbuffer-related gaps.
    if (kind == "cbuffer_declaration" || kind == "buffer_declaration" || kind == "declaration" ||
        kind == "global_variable_declaration" || kind == "function_definition") {
        const auto text = node_text(node, bytes);
        const bool is_cbuffer_decl = text.find("cbuffer") != std::string_view::npos ||
                                     text.find("ConstantBuffer<") != std::string_view::npos;
        if (is_cbuffer_decl && mentions_long_vector(text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message =
                std::string{"long vector (N >= 5) at a cbuffer / ConstantBuffer boundary; " +
                            std::string{"SM 6.9 long-vector spec restricts long vectors to "} +
                            "in-shader compute use -- split into a `float4` array"};
            ctx.emit(std::move(diag));
            return;
        }
    }
    if (kind == "field_declaration") {
        const auto text = node_text(node, bytes);
        if (mentions_long_vector(text) && mentions_io_semantic(text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message =
                std::string{"long vector (N >= 5) on a graphics IO signature element; " +
                            std::string{"SM 6.9 long-vector spec restricts IO signatures to "} +
                            "1/2/3/4-wide vectors -- split across multiple slots"};
            ctx.emit(std::move(diag));
            return;
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LongVectorInCbufferOrSignature : public Rule {
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

std::unique_ptr<Rule> make_long_vector_in_cbuffer_or_signature() {
    return std::make_unique<LongVectorInCbufferOrSignature>();
}

}  // namespace shader_clippy::rules
