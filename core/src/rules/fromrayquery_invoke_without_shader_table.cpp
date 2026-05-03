// fromrayquery-invoke-without-shader-table
//
// Detects a `dx::HitObject::FromRayQuery(...)` value that is invoked
// (`.Invoke(...)`) without an intervening `.SetShaderTableIndex(...)` call
// on the same HitObject. Per SER spec 0027, FromRayQuery-constructed
// HitObjects carry no shader-table index and require an explicit assignment
// before they can be invoked.
//
// Stage: Ast (forward-compatible-stub for Phase 4 definite-assignment
// analysis).
//
// The full rule needs CFG-based definite-assignment tracking on each
// HitObject local across every path between construction and invocation;
// that infrastructure ships in Phase 4. The Phase 3 stub matches the
// straight-line pattern: a function that contains both `FromRayQuery(` and
// `.Invoke(`, without `SetShaderTableIndex` anywhere between them in source
// order.

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

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "fromrayquery-invoke-without-shader-table";
constexpr std::string_view k_category = "ser";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const auto from_pos = fn_text.find("FromRayQuery(");
        if (from_pos != std::string_view::npos) {
            // Look for `.Invoke(` AFTER the construction.
            const auto invoke_pos = fn_text.find(".Invoke(", from_pos);
            if (invoke_pos != std::string_view::npos) {
                // Look for SetShaderTableIndex between the two.
                const auto sti_pos = fn_text.find("SetShaderTableIndex", from_pos);
                const bool has_sti = sti_pos != std::string_view::npos && sti_pos < invoke_pos;
                if (!has_sti) {
                    const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                    const auto invoke_lo = node_lo + static_cast<std::uint32_t>(invoke_pos + 1);
                    const auto invoke_hi =
                        invoke_lo + static_cast<std::uint32_t>(std::string_view{"Invoke"}.size());

                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Error;
                    diag.primary_span = Span{.source = tree.source_id(),
                                             .bytes = ByteSpan{.lo = invoke_lo, .hi = invoke_hi}};
                    diag.message = std::string{
                        "HitObject from `FromRayQuery` requires "
                        "`SetShaderTableIndex(...)` before `Invoke(...)` "
                        "(SER spec 0027); the inline RayQuery does not record "
                        "a shader table"};
                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class FromRayQueryInvokeWithoutShaderTable : public Rule {
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

std::unique_ptr<Rule> make_fromrayquery_invoke_without_shader_table() {
    return std::make_unique<FromRayQueryInvokeWithoutShaderTable>();
}

}  // namespace shader_clippy::rules
