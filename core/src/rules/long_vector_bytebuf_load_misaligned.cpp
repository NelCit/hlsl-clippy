// long-vector-bytebuf-load-misaligned
//
// Detects a `<ByteAddressBuffer>.Load<vector<T, N>>(<offset>)` (with
// `N >= 5`) where `<offset>` is a literal integer that is not 16-byte
// aligned. The SM 6.9 long-vector spec marks misaligned long-vector loads
// as either degraded (split transactions) or undefined (fault) depending on
// implementation.
//
// Stage: Ast (forward-compatible-stub).
//
// We check the literal offset against a 16-byte alignment floor (the
// component-type-aware variant -- 32-byte alignment for FP32 long vectors
// with N >= 8 -- waits for the bridge to surface buffer-load template
// arguments via reflection).

#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

constexpr std::string_view k_rule_id = "long-vector-bytebuf-load-misaligned";
constexpr std::string_view k_category = "long-vectors";

constexpr std::uint32_t k_min_alignment = 16U;

[[nodiscard]] bool template_arg_long_vector(std::string_view text) noexcept {
    const auto load_pos = text.find(".Load<");
    if (load_pos == std::string_view::npos) {
        return false;
    }
    const auto open = load_pos + std::string_view{".Load<"}.size();
    auto inner = text.substr(open);
    // Look for `vector<T, N>` with N >= 5 inside the template args.
    auto pos = inner.find("vector<");
    while (pos != std::string_view::npos) {
        const auto comma = inner.find(',', pos);
        const auto close = inner.find('>', pos);
        if (comma != std::string_view::npos && close != std::string_view::npos && comma < close) {
            auto p = comma + 1U;
            while (p < inner.size() && (inner[p] == ' ' || inner[p] == '\t')) {
                ++p;
            }
            std::uint32_t n = 0;
            bool any = false;
            while (p < inner.size() && inner[p] >= '0' && inner[p] <= '9') {
                n = n * 10U + static_cast<std::uint32_t>(inner[p] - '0');
                any = true;
                ++p;
            }
            if (any && n >= 5U) {
                return true;
            }
        }
        pos = inner.find("vector<", pos + 1U);
    }
    return false;
}

[[nodiscard]] bool parse_literal_offset(std::string_view call_text,
                                        std::uint32_t& offset_out) noexcept {
    // Walk past .Load<...>( to the argument list, then parse a leading
    // integer literal.
    const auto load = call_text.find(".Load<");
    if (load == std::string_view::npos) {
        return false;
    }
    const auto template_close = call_text.find(">(", load);
    if (template_close == std::string_view::npos) {
        return false;
    }
    auto p = template_close + 2U;
    while (p < call_text.size() && (call_text[p] == ' ' || call_text[p] == '\t')) {
        ++p;
    }
    std::uint32_t v = 0;
    bool any = false;
    while (p < call_text.size() && call_text[p] >= '0' && call_text[p] <= '9') {
        v = v * 10U + static_cast<std::uint32_t>(call_text[p] - '0');
        any = true;
        ++p;
    }
    if (!any) {
        return false;
    }
    offset_out = v;
    return true;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        if (template_arg_long_vector(call_text)) {
            std::uint32_t off = 0;
            if (parse_literal_offset(call_text, off) && (off % k_min_alignment) != 0U) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message =
                    std::string{"ByteAddressBuffer.Load<vector<T, N>>(...) with offset "} +
                    std::to_string(off) + " is not 16-byte aligned; long-vector " +
                    "loads either split transactions or fault on misalignment " +
                    "(SM 6.9 long-vector spec)";
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LongVectorBytebufLoadMisaligned : public Rule {
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

std::unique_ptr<Rule> make_long_vector_bytebuf_load_misaligned() {
    return std::make_unique<LongVectorBytebufLoadMisaligned>();
}

}  // namespace shader_clippy::rules
