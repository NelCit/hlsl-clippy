// divergent-buffer-index-on-uniform-resource
//
// Detects an indexed buffer access `buf[i]` where `buf` is a uniformly bound
// resource (`Buffer<T>`, `StructuredBuffer<T>`, `ByteAddressBuffer`, or
// `ConstantBuffer<T>`) and `i` is wave-divergent per the uniformity oracle.
// On RDNA / Ada / Xe-HPG this forces every lane onto the vector path,
// breaking the K$/scalar-cache fast-path the resource binding promised.
//
// Stage: ControlFlow.
//
// Detection plan:
//   1. Scan the AST for top-level resource declarations of one of the
//      uniform-bound kinds. Resources wrapped in `[NonUniformResourceIndex]`
//      or whose declaration text contains the marker token are skipped --
//      that case is the inverse hazard handled by `non-uniform-resource-
//      index`.
//   2. For each uniform-bound resource, walk the source for accesses of the
//      form `<name>[<expr>]`. For each match, ask the uniformity oracle the
//      uniformity of the index expression's span. When the oracle returns
//      `Divergent`, emit on the indexing expression's span.
//
// Forward-compatible stub note: this rule's CFG-stage hook re-uses the
// `uniformity::is_divergent` helper from `rules/util/uniformity.hpp` rather
// than open-coding the oracle plumbing. The helper returns `false` on
// `Unknown`, which keeps the rule conservative against false positives on
// expressions the oracle cannot classify yet (per ADR 0013 §"Decision
// Outcome" point 6).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/uniformity.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "divergent-buffer-index-on-uniform-resource";
constexpr std::string_view k_category = "bindings";
constexpr std::string_view k_marker = "NonUniformResourceIndex";

constexpr std::array<std::string_view, 4> k_uniform_kinds{
    "Buffer",
    "StructuredBuffer",
    "ByteAddressBuffer",
    "ConstantBuffer",
};

[[nodiscard]] std::size_t find_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0U;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return std::string_view::npos;
        }
        const bool ok_left = (found == 0U) || !is_id_char(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || !is_id_char(text[end]);
        if (ok_left && ok_right) {
            return found;
        }
        pos = found + 1U;
    }
    return std::string_view::npos;
}

/// Parse a uniform-bound resource declaration: returns the matched type-
/// keyword and the declared identifier name, or empty views when the
/// declaration does not match one of `k_uniform_kinds`. Also returns
/// `true` via `wraps_marker` when the declaration text contains the
/// `NonUniformResourceIndex` marker (which means the rule must skip the
/// resource).
[[nodiscard]] bool parse_uniform_decl(std::string_view decl_text,
                                      std::string_view& matched_kind,
                                      std::string_view& name) noexcept {
    for (const auto kind : k_uniform_kinds) {
        const auto kind_pos = find_keyword(decl_text, kind);
        if (kind_pos == std::string_view::npos) {
            continue;
        }
        // Reject if the source has been pre-marked NonUniformResourceIndex.
        if (find_keyword(decl_text, k_marker) != std::string_view::npos) {
            return false;
        }
        std::size_t i = kind_pos + kind.size();
        if (i < decl_text.size() && decl_text[i] == '<') {
            int depth = 0;
            while (i < decl_text.size()) {
                if (decl_text[i] == '<') {
                    ++depth;
                } else if (decl_text[i] == '>') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
                ++i;
            }
        }
        while (i < decl_text.size() && (decl_text[i] == ' ' || decl_text[i] == '\t')) {
            ++i;
        }
        const std::size_t name_start = i;
        while (i < decl_text.size() && is_id_char(decl_text[i])) {
            ++i;
        }
        if (name_start == i) {
            return false;
        }
        matched_kind = kind;
        name = decl_text.substr(name_start, i - name_start);
        return true;
    }
    return false;
}

/// Collect every uniformly-bound resource name in the source.
struct UniformBinding {
    std::string name;
    std::string kind;
    std::uint32_t decl_lo = 0U;
    std::uint32_t decl_hi = 0U;
};

void collect_uniform_bindings(::TSNode node,
                              std::string_view bytes,
                              std::vector<UniformBinding>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto decl_text = node_text(node, bytes);
        std::string_view matched_kind;
        std::string_view name;
        if (parse_uniform_decl(decl_text, matched_kind, name) && !name.empty()) {
            UniformBinding b;
            b.name = std::string{name};
            b.kind = std::string{matched_kind};
            b.decl_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
            b.decl_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
            out.push_back(std::move(b));
        }
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_uniform_bindings(::ts_node_child(node, i), bytes, out);
    }
}

class DivergentBufferIndexOnUniformResource : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        std::vector<UniformBinding> bindings;
        collect_uniform_bindings(::ts_tree_root_node(tree.raw_tree()), bytes, bindings);
        if (bindings.empty()) {
            return;
        }
        for (const auto& b : bindings) {
            std::size_t pos = 0U;
            while (pos <= bytes.size()) {
                const auto found = bytes.find(b.name, pos);
                if (found == std::string_view::npos) {
                    break;
                }
                const std::size_t end = found + b.name.size();
                const bool ok_left = (found == 0U) || !is_id_char(bytes[found - 1U]);
                const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
                const auto abs = static_cast<std::uint32_t>(found);
                if (!ok_left || !ok_right || (abs >= b.decl_lo && abs < b.decl_hi)) {
                    pos = found + 1U;
                    continue;
                }
                std::size_t k = end;
                while (k < bytes.size() && (bytes[k] == ' ' || bytes[k] == '\t')) {
                    ++k;
                }
                if (k >= bytes.size() || bytes[k] != '[') {
                    pos = end;
                    continue;
                }
                int depth = 0;
                std::size_t j = k;
                while (j < bytes.size()) {
                    if (bytes[j] == '[') {
                        ++depth;
                    } else if (bytes[j] == ']') {
                        --depth;
                        if (depth == 0) {
                            break;
                        }
                    }
                    ++j;
                }
                if (j >= bytes.size()) {
                    break;
                }
                const auto idx_lo = static_cast<std::uint32_t>(k + 1U);
                const auto idx_hi = static_cast<std::uint32_t>(j);
                const Span idx_span{
                    .source = tree.source_id(),
                    .bytes = ByteSpan{.lo = idx_lo, .hi = idx_hi},
                };
                if (util::is_divergent(cfg, idx_span)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{
                        .source = tree.source_id(),
                        .bytes = ByteSpan{.lo = abs, .hi = static_cast<std::uint32_t>(j + 1U)}};
                    diag.message = std::string{"`"} + b.kind + "<...>` `" + b.name +
                                   "` is bound uniformly but the index expression is wave-"
                                   "divergent -- on RDNA / Ada / Xe-HPG this forces every lane "
                                   "onto the vector cache path and loses the K$/scalar fast-path "
                                   "the binding promised; hoist the index to a uniform value or "
                                   "switch to a typed-buffer view designed for divergent loads";
                    ctx.emit(std::move(diag));
                }
                pos = static_cast<std::size_t>(j) + 1U;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_divergent_buffer_index_on_uniform_resource() {
    return std::make_unique<DivergentBufferIndexOnUniformResource>();
}

}  // namespace shader_clippy::rules
