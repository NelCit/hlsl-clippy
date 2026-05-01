// cbuffer-divergent-index
//
// Detects cbuffer / immediate-constant-buffer reads with a divergent index.
// The constant cache (K$ on RDNA, scalar cache on Ada, similar on Xe-HPG) is
// optimised for wave-uniform addressing; a divergent index serialises the
// load across the wave.
//
// Stage: ControlFlow. Uses `uniformity::is_divergent` on the index expression
// of `subscript_expression` nodes whose receiver is a cbuffer field array.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/uniformity.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "cbuffer-divergent-index";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::unordered_set<std::string> collect_cbuffer_names(std::string_view bytes) {
    std::unordered_set<std::string> out;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto found = bytes.find("cbuffer", pos);
        if (found == std::string_view::npos)
            return out;
        if (found > 0 && is_id_char(bytes[found - 1])) {
            pos = found + 1;
            continue;
        }
        std::size_t i = found + std::string_view{"cbuffer"}.size();
        if (i < bytes.size() && is_id_char(bytes[i])) {
            pos = i;
            continue;
        }
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        std::size_t name_start = i;
        while (i < bytes.size() && is_id_char(bytes[i]))
            ++i;
        if (i > name_start) {
            out.emplace(std::string{bytes.substr(name_start, i - name_start)});
        }
        pos = i;
    }
    return out;
}

void collect_cb_subscripts(::TSNode node,
                           std::string_view bytes,
                           const std::unordered_set<std::string>& cb_names,
                           std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "subscript_expression") {
        const auto recv = ::ts_node_named_child(node, 0);
        if (!::ts_node_is_null(recv) && node_kind(recv) == "field_expression") {
            // recv is `<cbName>.<arrayField>`.
            const auto outer = ::ts_node_named_child(recv, 0);
            const auto outer_text = node_text(outer, bytes);
            if (cb_names.contains(std::string{outer_text})) {
                out.push_back(node);
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_cb_subscripts(::ts_node_child(node, i), bytes, cb_names, out);
    }
}

class CbufferDivergentIndex : public Rule {
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
        const auto cb_names = collect_cbuffer_names(bytes);
        if (cb_names.empty())
            return;
        std::vector<::TSNode> subs;
        collect_cb_subscripts(::ts_tree_root_node(tree.raw_tree()), bytes, cb_names, subs);
        for (const auto sub : subs) {
            const auto idx = ::ts_node_named_child(sub, 1);
            const auto idx_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(idx)};
            if (!util::is_divergent(cfg, idx_span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(sub)};
            diag.message = std::string{
                "cbuffer indexed by a divergent expression -- the constant "
                "cache (K$ / scalar cache) serialises divergent loads on "
                "RDNA / Ada / Xe-HPG; consider scalarising via "
                "WaveActiveAllEqual or moving the array into a typed buffer"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "if every lane in the wave shares the same index, gate with "
                "WaveActiveAllEqual; otherwise move the array out of cbuffer "
                "into a typed Buffer<T> -- the texture-cache path tolerates "
                "divergent loads"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_cbuffer_divergent_index() {
    return std::make_unique<CbufferDivergentIndex>();
}

}  // namespace hlsl_clippy::rules
