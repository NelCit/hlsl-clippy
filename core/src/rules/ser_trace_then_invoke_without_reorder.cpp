// ser-trace-then-invoke-without-reorder
//
// Detects a `dx::HitObject::TraceRay` (or `FromRayQuery`) construction
// followed by an `.Invoke(...)` call on the same HitObject without an
// intervening `dx::MaybeReorderThread(...)`. The HitObject path pays a small
// fixed overhead vs. plain `TraceRay`; that overhead pays off only when a
// reorder happens.
//
// Stage: Ast (forward-compatible-stub for Phase 4 reachability analysis).
//
// The full rule needs CFG-based reachability between the two call sites on
// every path through the function. The Phase 3 stub matches the straight-
// line pattern: a function that contains both `HitObject::TraceRay(` (or
// `HitObject::FromRayQuery(`) and `.Invoke(`, without `MaybeReorderThread`
// between them in source order.

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

constexpr std::string_view k_rule_id = "ser-trace-then-invoke-without-reorder";
constexpr std::string_view k_category = "ser";

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

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const auto trace_pos = fn_text.find("HitObject::TraceRay(");
        const auto from_pos = fn_text.find("HitObject::FromRayQuery(");
        const auto ctor_pos =
            std::min(trace_pos == std::string_view::npos ? std::string_view::npos : trace_pos,
                     from_pos == std::string_view::npos ? std::string_view::npos : from_pos);
        if (ctor_pos == std::string_view::npos) {
            const std::uint32_t count = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < count; ++i) {
                walk(::ts_node_child(node, i), bytes, tree, ctx);
            }
            return;
        }
        const auto invoke_pos = fn_text.find(".Invoke(", ctor_pos);
        if (invoke_pos != std::string_view::npos) {
            const auto reorder_pos = fn_text.find("MaybeReorderThread", ctor_pos);
            const bool has_reorder =
                reorder_pos != std::string_view::npos && reorder_pos < invoke_pos;
            if (!has_reorder) {
                const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                const auto invoke_lo = node_lo + static_cast<std::uint32_t>(invoke_pos + 1);
                const auto invoke_hi =
                    invoke_lo + static_cast<std::uint32_t>(std::string_view{"Invoke"}.size());

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{.lo = invoke_lo, .hi = invoke_hi}};
                diag.message = std::string{
                    "HitObject `TraceRay` -> `Invoke` without an intervening "
                    "`MaybeReorderThread` pays the HitObject overhead without "
                    "recovering it; either insert `MaybeReorderThread` or use "
                    "plain `TraceRay`"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SerTraceThenInvokeWithoutReorder : public Rule {
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

std::unique_ptr<Rule> make_ser_trace_then_invoke_without_reorder() {
    return std::make_unique<SerTraceThenInvokeWithoutReorder>();
}

}  // namespace hlsl_clippy::rules
