// quadany-quadall-opportunity
//
// Detects an `if (cond)` whose condition is per-lane (quad-divergent) and
// whose body issues at least one derivative-bearing operation. The
// opportunity is to wrap the condition in `QuadAny(cond)` so that helper
// lanes participate in the 2x2 quad and derivatives stay valid. Per ADR
// 0011 §Phase 4 (rule pack C) and the rule's doc page.
//
// Stage: `ControlFlow`. The rule pairs an AST scan over `if_statement`
// nodes with a `uniformity::is_divergent` query on the branch condition.
// When the condition is divergent (or unanalysed) AND the body contains
// a derivative-bearing call, the opportunity diagnostic fires.
//
// Detection:
//   1. Walk every `if_statement` in the source.
//   2. Inspect its body text. If no derivative-bearing intrinsic
//      (`Sample`, `SampleBias`, `SampleGrad`, `ddx`, `ddy`, ...) is
//      present, skip.
//   3. Ask the uniformity oracle whether the branch condition span is
//      `Divergent`. If so, emit on the `if` statement.
//
// Conservatism contract: the oracle returns `Unknown` for spans it has
// not seen (e.g. expressions involving function calls). We do NOT treat
// `Unknown` as divergent here -- false-positive cost is high since this
// rule is suggestion-grade. If a future engine pass tightens the oracle,
// the rule's hit-rate goes up automatically.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

constexpr std::string_view k_rule_id = "quadany-quadall-opportunity";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 7> k_derivative_calls{
    "Sample",
    "SampleBias",
    "SampleGrad",
    "ddx",
    "ddy",
    "ddx_fine",
    "ddy_fine",
};

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_token(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        // Right boundary may also be `(` or `.` for member-call shapes.
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]) ||
                              text[end] == '(' || text[end] == '.';
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// True when `text` contains at least one derivative-bearing intrinsic. The
/// match is conservative against partial substrings (`SampleCmp` is not in
/// the list, since `SampleCmp` is a comparison-sampler call whose
/// derivative-need depends on the variant).
[[nodiscard]] bool body_uses_derivatives(std::string_view text) noexcept {
    for (const auto call : k_derivative_calls) {
        if (has_token(text, call)) {
            return true;
        }
    }
    return false;
}

/// True when `text` already wraps the condition in QuadAny / QuadAll, in
/// which case the rule must NOT fire (the author has already applied the
/// hint).
[[nodiscard]] bool condition_already_wrapped(std::string_view text) noexcept {
    return has_token(text, "QuadAny") || has_token(text, "QuadAll");
}

void scan_ifs(::TSNode node,
              std::string_view bytes,
              const ControlFlowInfo& cfg,
              const AstTree& tree,
              RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "if_statement") {
        // Find the condition expression and the consequence (then-body).
        const ::TSNode cond = ::ts_node_child_by_field_name(node, "condition", 9);
        const ::TSNode body = ::ts_node_child_by_field_name(node, "consequence", 11);
        const auto cond_text = node_text(cond, bytes);
        const auto body_text = node_text(body, bytes);

        const bool wrapped = condition_already_wrapped(cond_text);
        const bool deriv = body_uses_derivatives(body_text);

        if (!wrapped && deriv && !::ts_node_is_null(cond)) {
            const Span cond_span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(cond),
            };
            if (util::is_divergent(cfg, cond_span)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "per-lane `if` containing derivative-bearing intrinsic (Sample / ddx / ddy / "
                    "...) -- consider wrapping the condition in `QuadAny(...)` so all four quad "
                    "lanes participate as helpers and derivatives have valid neighbour samples; "
                    "without it, retired quad lanes feed undefined derivative inputs and produce "
                    "mip-aliasing or seam artefacts at the branch boundary"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_ifs(::ts_node_child(node, i), bytes, cfg, tree, ctx);
    }
}

class QuadAnyQuadAllOpportunity : public Rule {
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
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        scan_ifs(root, tree.source_bytes(), cfg, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_quadany_quadall_opportunity() {
    return std::make_unique<QuadAnyQuadAllOpportunity>();
}

}  // namespace hlsl_clippy::rules
