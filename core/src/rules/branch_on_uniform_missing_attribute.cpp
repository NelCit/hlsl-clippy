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

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

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
        // Heuristic uniform signal: condition contains `.` (likely struct
        // field access on a cbuffer) and no SV_-divergent reference.
        const bool has_field_access = cond_text.find('.') != std::string_view::npos;
        if (!seems_divergent && has_field_access) {
            const auto stmt_lo = static_cast<std::size_t>(::ts_node_start_byte(node));
            const auto prefix = prefix_text(bytes, stmt_lo);
            const bool has_branch_attr = prefix.find("[branch]") != std::string_view::npos;
            const bool has_flatten_attr = prefix.find("[flatten]") != std::string_view::npos;
            if (!has_branch_attr && !has_flatten_attr) {
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

}  // namespace hlsl_clippy::rules
