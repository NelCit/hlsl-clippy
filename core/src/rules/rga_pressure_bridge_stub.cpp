// rga-pressure-bridge-stub
//
// Per ADR 0018 §4.3, this is an infrastructure investment, not a rule
// per se. The rule emits a one-shot informational note per source
// compiled on RDNA targets (any RDNA gen via the experimental gate),
// pointing to the future `tools/rga-bridge` that will produce more
// accurate VGPR counts than the AST heuristic. Keeps the candidate
// alive in the rule catalog while the bridge is being built.
//
// Stage: Reflection. Gated behind `[experimental.target = rdna4]`.
// Suggestion-grade. Severity::Note.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "rga-pressure-bridge-stub";
constexpr std::string_view k_category = "rdna4";

class RgaPressureBridgeStub : public Rule {
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
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept override {
        return ExperimentalTarget::Rdna4;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Note;
        diag.primary_span = Span{
            .source = tree.source_id(),
            .bytes = ByteSpan{0U, 0U},
        };
        diag.message =
            "(info) RDNA experimental target selected -- a future "
            "`tools/rga-bridge` will produce ground-truth per-block VGPR counts "
            "from RGA's CSV output; the current `vgpr-pressure-warning` rule is "
            "an AST heuristic. Track ADR 0018 §4.3.";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_rga_pressure_bridge_stub() {
    return std::make_unique<RgaPressureBridgeStub>();
}

}  // namespace shader_clippy::rules
