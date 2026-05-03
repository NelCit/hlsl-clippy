// long-vector-non-elementwise-intrinsic
//
// Detects a call to a non-elementwise HLSL intrinsic (`cross`, `length`,
// `normalize`, `transpose`, `determinant`, matrix `mul`) applied to a
// `vector<T, N>` with `N >= 5`. Pure-AST: intrinsic name + literal long
// vector type on the argument.
//
// Stage: Ast.
//
// The Phase 2 ADR slot for this rule places it in pack B, but the user pack
// for SM 6.9 includes it as the long-vector group's hard-error anchor. We
// implement it here without depending on Phase 4 reflection: the trigger is
// the source pattern "intrinsic-name(... vector<T, N>{>=5} ...)" or a call
// whose argument is a typedef like `float8`/`float16` that the spec
// reserves for long-vector aliases.

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

constexpr std::string_view k_rule_id = "long-vector-non-elementwise-intrinsic";
constexpr std::string_view k_category = "long-vectors";

constexpr std::array<std::string_view, 6> k_intrinsics{
    "cross",
    "length",
    "normalize",
    "transpose",
    "determinant",
    "mul",
};

/// True iff `text` mentions a `vector<T, N>` parameterisation with `N >= 5`,
/// or one of the canonical long-vector aliases `floatN`/`int N`/`uintN`/
/// `halfN` with `N >= 5` (`float5`, `float8`, `float16`, etc.).
[[nodiscard]] bool mentions_long_vector(std::string_view text) noexcept {
    // Look for `vector<...,N>` with N >= 5.
    auto pos = text.find("vector<");
    while (pos != std::string_view::npos) {
        const auto comma = text.find(',', pos);
        const auto close = text.find('>', pos);
        if (comma != std::string_view::npos && close != std::string_view::npos && comma < close) {
            // Skip whitespace after comma.
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
    // Check long-vector type aliases. For each prefix in {float, int, uint,
    // half, bool, double}, find prefix followed by digits >=5, and require
    // that the byte before the prefix is a non-identifier char so we don't
    // hit substrings like `g_float8`.
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
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        const auto open = call_text.find('(');
        if (open != std::string_view::npos) {
            // Take the function-name portion as the leading identifier prefix.
            std::size_t name_lo = 0;
            std::size_t name_hi = open;
            // Trim trailing whitespace.
            while (name_hi > name_lo &&
                   (call_text[name_hi - 1] == ' ' || call_text[name_hi - 1] == '\t')) {
                --name_hi;
            }
            // Find the start of the leading identifier.
            std::size_t i = name_hi;
            while (i > name_lo && is_id_char(call_text[i - 1])) {
                --i;
            }
            const auto name = call_text.substr(i, name_hi - i);
            for (const auto intrin : k_intrinsics) {
                if (name == intrin) {
                    if (mentions_long_vector(call_text)) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Error;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message =
                            std::string{"`"} + std::string{intrin} +
                            "` is not defined for long vectors (N >= 5); SM 6.9 " +
                            "long-vector spec restricts these intrinsics to 1/2/3/4-wide vectors";
                        ctx.emit(std::move(diag));
                    }
                    break;
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LongVectorNonElementwiseIntrinsic : public Rule {
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

std::unique_ptr<Rule> make_long_vector_non_elementwise_intrinsic() {
    return std::make_unique<LongVectorNonElementwiseIntrinsic>();
}

}  // namespace shader_clippy::rules
