// hitobject-passed-to-non-inlined-fn
//
// Detects a `dx::HitObject` value passed as an argument to (or returned from)
// a function that the compiler cannot prove is inlined into its raygen
// caller. SM 6.9 SER spec 0027 requires HitObject lifetimes to stay in an
// inlined call chain; non-inlined boundaries are undefined behaviour.
//
// Stage: Ast (forward-compatible-stub for Phase 4 inter-procedural analysis).
//
// The full implementation requires Phase 4 call-graph + inlining analysis
// that is not yet shipped (`ControlFlowInfo` does not currently expose
// per-callee inlining state). This Phase 3 stub catches the trivially
// observable trigger: a function declared `[noinline]` (or the `inline`-not-
// asserted form) with a `dx::HitObject` parameter or return type. The
// inter-procedural cases (recursion, indirect calls) are deferred.

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

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "hitobject-passed-to-non-inlined-fn";
constexpr std::string_view k_category = "ser";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        // Trivial trigger: function carries `[noinline]` AND mentions
        // `HitObject` in its signature region (before the body opens).
        const auto noinline_pos = fn_text.find("noinline");
        if (noinline_pos != std::string_view::npos) {
            const auto body_pos = fn_text.find('{');
            const auto sig_text =
                (body_pos != std::string_view::npos) ? fn_text.substr(0, body_pos) : fn_text;
            if (sig_text.find("HitObject") != std::string_view::npos) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "`dx::HitObject` cannot cross a non-inlined function "
                    "boundary (SER spec 0027); this `[noinline]` function "
                    "takes or returns a HitObject"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class HitObjectPassedToNonInlinedFn : public Rule {
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

std::unique_ptr<Rule> make_hitobject_passed_to_non_inlined_fn() {
    return std::make_unique<HitObjectPassedToNonInlinedFn>();
}

}  // namespace hlsl_clippy::rules
