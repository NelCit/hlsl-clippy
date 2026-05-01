// groupshared-stride-32-bank-conflict
//
// Detects groupshared array accesses with stride 32 that hit a 32-bank LDS
// serialisation hazard on RDNA / Turing / Ada / Xe-HPG. The canonical bad
// pattern is `gs[tid * 32 + k]` -- every active lane lands in the same bank
// and the access serialises 32-fold.
//
// Stage: Ast. Textual detection on `subscript_expression` nodes whose index
// expression contains `* 32` (or `<< 5`) followed by a constant offset.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "groupshared-stride-32-bank-conflict";
constexpr std::string_view k_category = "workgroup";

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

/// Collect `groupshared` declared identifier names by scanning the source.
[[nodiscard]] std::vector<std::string> collect_gs_decls(std::string_view bytes) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto found = bytes.find("groupshared", pos);
        if (found == std::string_view::npos)
            return out;
        // Walk forward to the identifier name. Skip qualifier / type tokens
        // until we see a `[` or `;`. We pick up the last identifier-looking
        // token before the `[` / `;`.
        std::size_t i = found + std::string_view{"groupshared"}.size();
        while (i < bytes.size() && bytes[i] != ';' && bytes[i] != '[' && bytes[i] != '{') {
            ++i;
        }
        // Walk backwards from i to find an identifier token.
        std::size_t end = i;
        while (end > found && (bytes[end - 1] == ' ' || bytes[end - 1] == '\t'))
            --end;
        std::size_t start = end;
        while (start > found &&
               ((bytes[start - 1] >= 'a' && bytes[start - 1] <= 'z') ||
                (bytes[start - 1] >= 'A' && bytes[start - 1] <= 'Z') ||
                (bytes[start - 1] >= '0' && bytes[start - 1] <= '9') || bytes[start - 1] == '_')) {
            --start;
        }
        if (end > start) {
            out.emplace_back(bytes.substr(start, end - start));
        }
        pos = found + 1;
    }
    return out;
}

/// True when `idx_text` looks like a `tid * 32 + k` / `tid << 5 + k` pattern.
[[nodiscard]] bool is_stride32_pattern(std::string_view idx_text) noexcept {
    return idx_text.find("* 32") != std::string_view::npos ||
           idx_text.find("*32") != std::string_view::npos ||
           idx_text.find("<< 5") != std::string_view::npos ||
           idx_text.find("<<5") != std::string_view::npos;
}

void walk(::TSNode node,
          std::string_view bytes,
          const AstTree& tree,
          RuleContext& ctx,
          const std::vector<std::string>& gs_names) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "subscript_expression") {
        const auto receiver = ::ts_node_named_child(node, 0);
        const auto idx = ::ts_node_named_child(node, 1);
        const auto recv_text = node_text(receiver, bytes);
        const auto idx_text = node_text(idx, bytes);
        bool is_gs = false;
        for (const auto& name : gs_names) {
            if (recv_text == name) {
                is_gs = true;
                break;
            }
        }
        if (is_gs && is_stride32_pattern(idx_text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "groupshared access with stride 32 -- every lane lands in the "
                "same LDS bank on RDNA/Ada/Xe-HPG (32 banks of 4 bytes); add a "
                "+1 padding to the array size to break the stride"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "pad the groupshared array (e.g. `gs[N*32+1]`) and re-index "
                "with the same `+1` skew -- the bank conflict drops from 32-way "
                "to none on every modern IHV"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx, gs_names);
    }
}

class GroupsharedStride32BankConflict : public Rule {
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
        const auto bytes = tree.source_bytes();
        const auto gs_names = collect_gs_decls(bytes);
        if (gs_names.empty())
            return;
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx, gs_names);
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_stride_32_bank_conflict() {
    return std::make_unique<GroupsharedStride32BankConflict>();
}

}  // namespace hlsl_clippy::rules
