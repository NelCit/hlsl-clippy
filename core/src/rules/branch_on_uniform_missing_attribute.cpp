// branch-on-uniform-missing-attribute
//
// Detects `if` statements whose condition references uniform-looking sources
// (cbuffer fields / push constants / `[WaveSize]`-pinned uniform reads) but
// which lack a `[branch]` attribute. Without the attribute the compiler may
// flatten the branch on AMD RDNA / Ada and execute both arms; `[branch]`
// keeps the compile-time hint that the runtime takes only one path.
//
// Heuristic Phase 4 stub (Ast): we fire on `if (CONDITION)` whose preceding
// attribute prefix omits `[branch]` AND whose condition mentions an
// identifier matching the textual pattern of a cbuffer field
// (`<name>.<field>`, capital-letter prefix common, or any identifier that is
// not in the SV_-divergent seed list). The full uniformity analysis lives in
// Phase 4 4b utilities.

#include <array>
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

constexpr std::string_view k_rule_id = "branch-on-uniform-missing-attribute";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 7> k_divergent_seeds{
    "SV_DispatchThreadID",
    "SV_GroupThreadID",
    "SV_GroupIndex",
    "SV_VertexID",
    "SV_InstanceID",
    "SV_PrimitiveID",
    "SV_SampleIndex",
};

/// Thread-id-like identifier seeds. When a branch condition references one
/// of these, treat the branch as inherently divergent (the parameter is
/// almost certainly bound to an SV_* semantic that we can't see textually
/// without a full parameter-declarator walk). Conservative on naming
/// conventions used across the corpus.
constexpr std::array<std::string_view, 13> k_thread_id_seeds{
    "tid",
    "gid",
    "gi",
    "dtid",
    "gtid",
    "DTid",
    "GTid",
    "GI",
    "groupIndex",
    "GroupIndex",
    "WaveGetLane",
    "lane",
    "laneIndex",
};

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// Return the prefix bytes (attributes) before `start`, walking back to the
/// previous semicolon / brace.
[[nodiscard]] std::string_view prefix_text(std::string_view bytes, std::size_t start) noexcept {
    std::size_t i = start;
    while (i > 0) {
        const char c = bytes[i - 1];
        if (c == ';' || c == '}' || c == '{')
            break;
        --i;
    }
    return bytes.substr(i, start - i);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "if_statement") {
        const auto cond = ::ts_node_child_by_field_name(node, "condition", 9);
        const auto cond_text = node_text(cond, bytes);
        // Skip if the condition references an inherently divergent semantic
        // -- those are the wrong target for `[branch]`.
        bool seems_divergent = false;
        for (const auto seed : k_divergent_seeds) {
            if (cond_text.find(seed) != std::string_view::npos) {
                seems_divergent = true;
                break;
            }
        }
        // Also skip if the condition references a thread-id-like local
        // (`tid.x`, `gi`, `dtid`, ...). The function parameter is almost
        // certainly bound to a divergent SV_* semantic that we cannot see
        // without a full parameter-declarator walk; the branch is divergent
        // and `[branch]` is the wrong hint.
        if (!seems_divergent) {
            for (const auto seed : k_thread_id_seeds) {
                if (has_keyword(cond_text, seed)) {
                    seems_divergent = true;
                    break;
                }
            }
        }
        // Heuristic uniform signal: condition contains `.` (likely struct
        // field access on a cbuffer) and no divergent reference.
        const bool has_field_access = cond_text.find('.') != std::string_view::npos;
        if (!seems_divergent && has_field_access) {
            // The grammar attaches `hlsl_attribute` to the `if_statement`
            // node itself, so the if-statement's own text already covers
            // any `[branch]` / `[flatten]` prefix the author wrote.
            // (Previously this rule walked the bytes BEFORE the
            // if-statement's start offset, which always misses the
            // attribute.) Inspect the leading bytes of the node up to the
            // `if` keyword.
            const auto stmt_text = node_text(node, bytes);
            std::string_view attr_prefix = stmt_text;
            const auto if_pos = stmt_text.find("if");
            if (if_pos != std::string_view::npos) {
                attr_prefix = stmt_text.substr(0, if_pos);
            }
            const bool has_branch_attr = attr_prefix.find("[branch]") != std::string_view::npos;
            const bool has_flatten_attr = attr_prefix.find("[flatten]") != std::string_view::npos;
            // Also defensively check the bytes immediately before the node
            // (in case a future grammar revision moves attributes outside).
            const auto stmt_lo = static_cast<std::size_t>(::ts_node_start_byte(node));
            const auto prefix = prefix_text(bytes, stmt_lo);
            const bool prefix_has_branch = prefix.find("[branch]") != std::string_view::npos;
            const bool prefix_has_flatten = prefix.find("[flatten]") != std::string_view::npos;
            if (!has_branch_attr && !has_flatten_attr && !prefix_has_branch &&
                !prefix_has_flatten) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "`if` whose condition looks dynamically uniform lacks an "
                    "explicit `[branch]` attribute -- without the hint the "
                    "compiler may flatten the branch and execute both arms"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "prefix the `if` with `[branch]` so the compiler keeps "
                    "the runtime jump on RDNA/Ada/Xe-HPG"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class BranchOnUniformMissingAttribute : public Rule {
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

std::unique_ptr<Rule> make_branch_on_uniform_missing_attribute() {
    return std::make_unique<BranchOnUniformMissingAttribute>();
}

}  // namespace shader_clippy::rules
