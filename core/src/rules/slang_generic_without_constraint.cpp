// slang-generic-without-constraint
//
// Detects `__generic<T>` (or template-form `<T>`) generic declarations that
// don't carry an interface-conformance constraint (`: ITrait`) on any of
// their type parameters. Constraint-less generics defer to runtime
// monomorphisation rather than compile-time specialisation, costing
// instruction-cache footprint and per-instantiation register pressure on
// every concrete invocation.
//
// Stage: Ast. Slang-only — gates on the parsed grammar pointer so the rule
// emits zero diagnostics on `.hlsl` sources (where `__generic` is not a
// recognised keyword and a top-level `template_type` named `__generic`
// would not appear).
//
// Empirical AST shape (probed against tree-sitter-slang v1.4.0 in
// sub-phase C dev):
//
//   `__generic<T> void f(T x) {}` parses as a top-level
//   `template_type` whose `name` field is a `type_identifier` with
//   text "__generic" and whose `arguments` field is a
//   `template_argument_list`. Each unconstrained type parameter shows
//   up as a `(type_descriptor type: (type_identifier))` child of the
//   argument list. A constraint surfaces as an
//   `interface_requirements` sibling: e.g. for
//   `__generic<T : ICompute>` the argument list contains
//   `(type_descriptor ...) (interface_requirements (identifier))`.
//
// The rule fires when the argument list contains zero
// `interface_requirements` children — i.e. NONE of the type parameters
// carry an interface-conformance bound.
//
// References:
//   - Slang language guide -- generics and interfaces.
//   - Slang user-guide §"Generics" — constraint-driven specialisation.

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

extern "C" {
const ::TSLanguage* tree_sitter_slang(void);
}

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "slang-generic-without-constraint";
constexpr std::string_view k_category = "slang-language";

[[nodiscard]] bool argument_list_has_constraint(::TSNode args) noexcept {
    const std::uint32_t cnt = ::ts_node_child_count(args);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        const auto child = ::ts_node_child(args, i);
        if (node_kind(child) == "interface_requirements") {
            return true;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "template_type") {
        const auto name = ::ts_node_child_by_field_name(node, "name", 4);
        const auto name_text = node_text(name, bytes);
        if (name_text == "__generic") {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && !argument_list_has_constraint(args)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message =
                    "`__generic<T>` declared without an interface-conformance "
                    "constraint -- Slang defers monomorphisation to use-site "
                    "rather than specialising at compile time, costing "
                    "instruction-cache footprint and per-instantiation register "
                    "pressure. Add a `: ITrait` bound to each type parameter "
                    "(e.g. `__generic<T : ICompute>`) so the compiler can "
                    "specialise per concrete type";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SlangGenericWithoutConstraint : public Rule {
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
        // Slang-only rule. Hard-gate on the parsed grammar pointer so the
        // rule stays silent on tree-sitter-hlsl AST.
        if (tree.language() != tree_sitter_slang()) {
            return;
        }
        const auto bytes = tree.source_bytes();
        // Cheap pre-filter: skip walking when the `__generic` keyword is
        // not present anywhere in the source.
        if (bytes.find("__generic") == std::string_view::npos) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_slang_generic_without_constraint() {
    return std::make_unique<SlangGenericWithoutConstraint>();
}

}  // namespace shader_clippy::rules
