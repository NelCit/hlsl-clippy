// getgroupwaveindex-without-wavesize-attribute
//
// Detects calls to `GetGroupWaveIndex()` / `GetGroupWaveCount()` (SM 6.10
// proposal 0048 Accepted) in a compute / mesh / amplification entry point
// that does NOT pin the wave size via `[WaveSize(N)]`. Per the proposal,
// the index/count semantics are only well-defined under an explicit wave
// size; without `[WaveSize]`, RDNA may run wave32 or wave64 and the index
// returned silently changes between hardware drivers.
//
// Stage: Ast. Walk the source for any call to `GetGroupWaveIndex` /
// `GetGroupWaveCount`; check the lexically enclosing function for a
// `[WaveSize(...)]` attribute; emit when missing. Reflection is not
// required because the rule's logic is local to one function.

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

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "getgroupwaveindex-without-wavesize-attribute";
constexpr std::string_view k_category = "sm6_10";

constexpr std::array<std::string_view, 2> k_intrinsics{
    "GetGroupWaveIndex",
    "GetGroupWaveCount",
};

/// Walk back from `start` to the closest preceding `;`, `}`, `{`, or
/// start-of-file. Returns the byte offset of the position just after that
/// boundary -- the start of any attribute-bearing prefix that may precede
/// a function declaration.
[[nodiscard]] std::size_t prefix_start(std::string_view bytes, std::size_t start) noexcept {
    std::size_t i = start;
    while (i > 0) {
        const char c = bytes[i - 1];
        if (c == ';' || c == '}' || c == '{') {
            break;
        }
        --i;
    }
    return i;
}

/// True when the prefix bytes contain a `[WaveSize` attribute (any form).
[[nodiscard]] bool prefix_has_wavesize(std::string_view prefix) noexcept {
    constexpr std::string_view k_attr = "WaveSize";
    std::size_t i = 0;
    while (i < prefix.size()) {
        if (prefix[i] != '[') {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < prefix.size() && (prefix[j] == ' ' || prefix[j] == '\t')) {
            ++j;
        }
        if (j + k_attr.size() > prefix.size()) {
            ++i;
            continue;
        }
        if (prefix.substr(j, k_attr.size()) == k_attr) {
            // Make sure it's a word boundary on the right.
            const std::size_t after = j + k_attr.size();
            if (after >= prefix.size() ||
                !(is_id_char(prefix[after]))) {
                return true;
            }
        }
        ++i;
    }
    return false;
}

/// Walk up the AST from `node` until we hit a `function_definition`. Returns
/// null if no enclosing function is found.
[[nodiscard]] ::TSNode enclosing_function(::TSNode node) noexcept {
    ::TSNode cur = node;
    while (!::ts_node_is_null(cur)) {
        if (node_kind(cur) == "function_definition") {
            return cur;
        }
        cur = ::ts_node_parent(cur);
    }
    return ::TSNode{};
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        bool match = false;
        for (const auto name : k_intrinsics) {
            if (fn_text == name) {
                match = true;
                break;
            }
        }
        if (match) {
            const auto fn_def = enclosing_function(node);
            bool has_wavesize = false;
            if (!::ts_node_is_null(fn_def)) {
                const auto fn_lo = static_cast<std::size_t>(::ts_node_start_byte(fn_def));
                const auto pref_lo = prefix_start(bytes, fn_lo);
                if (pref_lo < fn_lo) {
                    has_wavesize = prefix_has_wavesize(bytes.substr(pref_lo, fn_lo - pref_lo));
                }
            }
            if (!has_wavesize) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message =
                    std::string{"`"} + std::string{fn_text} +
                    "()` (SM 6.10) needs an explicit `[WaveSize(N)]` on the entry "
                    "point -- without it, RDNA may run wave32 or wave64 and the "
                    "index/count returned silently changes between IHV drivers";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class GetGroupWaveIndexWithoutWaveSizeAttribute : public Rule {
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

std::unique_ptr<Rule> make_getgroupwaveindex_without_wavesize_attribute() {
    return std::make_unique<GetGroupWaveIndexWithoutWaveSizeAttribute>();
}

}  // namespace hlsl_clippy::rules
