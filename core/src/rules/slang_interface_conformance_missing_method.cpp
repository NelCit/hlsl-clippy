// slang-interface-conformance-missing-method
//
// Detects an `extension Foo : IFoo {}` block whose body fails to implement
// every method declared on the conformed-to interface. Slang's diagnostic
// for missing-method-conformance is silent at module-definition level and
// only surfaces at use-site, so a developer can ship a "complete" extension
// for weeks before a downstream caller breaks.
//
// Stage: Ast. Slang-only.
//
// Detection plan (purely syntactic, no reflection):
//
//   1. Walk every `interface_specifier` in the translation unit. Collect
//      `(interface-name -> set-of-method-names)` pairs from the
//      `field_declaration_list` body. A method is any
//      `field_declaration` whose declarator is a `function_declarator`.
//   2. Walk every `extension_specifier` with a `base_class_clause` (the
//      `: IFoo` conformance form). For each conformed-to interface name
//      that we have a method-set for, intersect against the methods
//      defined inside the extension's `field_declaration_list` body. If
//      ANY interface method is missing from the extension body, fire.
//
// We deliberately do not consult Slang's reflection here -- the detection
// is purely syntactic so it runs without `--reflect`. False negatives
// (interface defined in another module / `import`-ed translation unit)
// are acceptable for the v1.5.0 baseline; tightening is a v1.5.x
// follow-up via reflection.
//
// References:
//   - Slang language guide §"Interfaces and Extensions".
//   - Slang user-guide §"Conformance".

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

extern "C" {
const ::TSLanguage* tree_sitter_slang(void);
}

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "slang-interface-conformance-missing-method";
constexpr std::string_view k_category = "slang-language";

/// Extract the leading identifier text of a `function_declarator` --
/// this is the method name. Returns an empty view if the declarator
/// shape is unexpected.
[[nodiscard]] std::string_view function_declarator_name(::TSNode fn_decl,
                                                        std::string_view bytes) noexcept {
    if (::ts_node_is_null(fn_decl)) {
        return {};
    }
    const auto inner = ::ts_node_child_by_field_name(fn_decl, "declarator", 10);
    if (!::ts_node_is_null(inner)) {
        const auto kind = node_kind(inner);
        if (kind == "field_identifier" || kind == "identifier") {
            return node_text(inner, bytes);
        }
    }
    return {};
}

/// Collect method names declared inside a `field_declaration_list` body.
/// Tree-sitter-slang surfaces interface-method DECLARATIONS as
/// `field_declaration` (no body) and extension-method DEFINITIONS as
/// `function_definition` (with body). Both are accepted — the rule just
/// needs the method name regardless of declaration vs definition shape.
[[nodiscard]] std::unordered_set<std::string> collect_method_names(::TSNode body,
                                                                   std::string_view bytes) {
    std::unordered_set<std::string> out;
    if (::ts_node_is_null(body)) {
        return out;
    }
    const std::uint32_t cnt = ::ts_node_child_count(body);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        const auto child = ::ts_node_child(body, i);
        const auto kind = node_kind(child);
        if (kind != "field_declaration" && kind != "function_definition") {
            continue;
        }
        const auto declarator = ::ts_node_child_by_field_name(child, "declarator", 10);
        if (::ts_node_is_null(declarator) || node_kind(declarator) != "function_declarator") {
            continue;
        }
        const auto name = function_declarator_name(declarator, bytes);
        if (!name.empty()) {
            out.emplace(name);
        }
    }
    return out;
}

/// Walk and record every `interface_specifier` in the translation unit:
/// (interface-name -> set-of-declared-method-names).
void collect_interfaces(::TSNode node,
                        std::string_view bytes,
                        std::unordered_map<std::string, std::unordered_set<std::string>>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "interface_specifier") {
        const auto name = ::ts_node_child_by_field_name(node, "name", 4);
        const auto body = ::ts_node_child_by_field_name(node, "body", 4);
        const auto iface_name = node_text(name, bytes);
        if (!iface_name.empty()) {
            out[std::string{iface_name}] = collect_method_names(body, bytes);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_interfaces(::ts_node_child(node, i), bytes, out);
    }
}

/// Extract every conformed-to interface name from a `base_class_clause`
/// child of an `extension_specifier`. The clause's tree-sitter shape
/// surfaces inheritance bases as `type_identifier` / `qualified_identifier`
/// children.
[[nodiscard]] std::vector<std::string_view> collect_base_interfaces(::TSNode ext_node,
                                                                    std::string_view bytes) {
    std::vector<std::string_view> out;
    const std::uint32_t cnt = ::ts_node_child_count(ext_node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        const auto child = ::ts_node_child(ext_node, i);
        if (node_kind(child) != "base_class_clause") {
            continue;
        }
        const std::uint32_t inner_cnt = ::ts_node_child_count(child);
        for (std::uint32_t j = 0; j < inner_cnt; ++j) {
            const auto inner = ::ts_node_child(child, j);
            const auto kind = node_kind(inner);
            if (kind == "type_identifier" || kind == "qualified_identifier") {
                out.push_back(node_text(inner, bytes));
            }
        }
    }
    return out;
}

void walk_extensions(
    ::TSNode node,
    std::string_view bytes,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& interfaces,
    const AstTree& tree,
    RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "extension_specifier") {
        const auto bases = collect_base_interfaces(node, bytes);
        if (!bases.empty()) {
            const auto body = ::ts_node_child_by_field_name(node, "body", 4);
            const auto provided = collect_method_names(body, bytes);
            for (const auto& base : bases) {
                const auto it = interfaces.find(std::string{base});
                if (it == interfaces.end()) {
                    // Interface not in this translation unit (likely
                    // imported); skip — too noisy to flag without
                    // module reflection.
                    continue;
                }
                for (const auto& required : it->second) {
                    if (provided.find(required) == provided.end()) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Error;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        std::string msg;
                        msg += "`extension` conformance to interface `";
                        msg += base;
                        msg += "` is missing method `";
                        msg += required;
                        msg +=
                            "()` -- Slang's missing-method diagnostic is "
                            "silent at definition site and only surfaces at "
                            "use-site, so this extension will compile until a "
                            "downstream caller invokes the missing method";
                        diag.message = std::move(msg);
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk_extensions(::ts_node_child(node, i), bytes, interfaces, tree, ctx);
    }
}

class SlangInterfaceConformanceMissingMethod : public Rule {
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
        if (tree.language() != tree_sitter_slang()) {
            return;
        }
        const auto bytes = tree.source_bytes();
        if (bytes.find("extension") == std::string_view::npos ||
            bytes.find("interface") == std::string_view::npos) {
            return;
        }
        std::unordered_map<std::string, std::unordered_set<std::string>> interfaces;
        const auto root = ::ts_tree_root_node(tree.raw_tree());
        collect_interfaces(root, bytes, interfaces);
        if (interfaces.empty()) {
            return;
        }
        walk_extensions(root, bytes, interfaces, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_slang_interface_conformance_missing_method() {
    return std::make_unique<SlangInterfaceConformanceMissingMethod>();
}

}  // namespace shader_clippy::rules
