// hitobject-stored-in-memory
//
// Detects a `dx::HitObject` value (the SM 6.9 SER type) stored into any memory
// location: a `groupshared` declaration, a UAV write, a globally-scoped
// variable, or a struct member that is itself stored. Spec proposal 0027
// restricts `dx::HitObject` to register-only lifetimes inside an inlined call
// chain rooted at a raygeneration shader.
//
// Stage: Ast (forward-compatible-stub for Reflection-driven analysis).
//
// The Slang reflection bridge today does not surface `dx::HitObject` as a
// distinguished type in `ReflectionInfo`, so this rule operates in a textual
// pre-flight mode: it walks declaration / field-declaration nodes and fires
// when a node's text references the `dx::HitObject` (or `HitObject`) type
// name in a storage context that is not a function-local register lifetime
// (groupshared / static / global declarations, struct fields). Once the
// bridge surfaces SER types we should re-implement this against
// `ReflectionInfo` directly.

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

constexpr std::string_view k_rule_id = "hitobject-stored-in-memory";
constexpr std::string_view k_category = "ser";
constexpr std::string_view k_hitobject_qualified = "dx::HitObject";
constexpr std::string_view k_hitobject_unqualified = "HitObject";

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

[[nodiscard]] bool mentions_hitobject(std::string_view text) noexcept {
    return text.find(k_hitobject_qualified) != std::string_view::npos ||
           text.find(k_hitobject_unqualified) != std::string_view::npos;
}

[[nodiscard]] bool mentions_storage_keyword(std::string_view text) noexcept {
    return text.find("groupshared") != std::string_view::npos ||
           text.find("static") != std::string_view::npos ||
           text.find("RWStructuredBuffer") != std::string_view::npos ||
           text.find("RWByteAddressBuffer") != std::string_view::npos;
}

void walk(::TSNode node,
          std::string_view bytes,
          const AstTree& tree,
          RuleContext& ctx,
          bool inside_function) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool entering_function = (kind == "function_definition");
    const bool inside_fn_now = inside_function || entering_function;

    // Top-level / struct-member declarations of HitObject -> always a memory
    // store. Within a function body we only fire if the declaration carries a
    // groupshared/static qualifier (locals are register-allocated by default
    // and this rule defers to the call-chain rule for those).
    const bool decl_like = (kind == "declaration" || kind == "field_declaration" ||
                            kind == "global_variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        if (mentions_hitobject(text)) {
            const bool is_global_or_field = !inside_function;
            const bool has_storage_kw = mentions_storage_keyword(text);
            if (is_global_or_field || has_storage_kw) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "`dx::HitObject` must be register-only inside an inlined "
                    "raygen call chain (SER spec 0027); storing it in memory "
                    "is undefined behaviour"};
                ctx.emit(std::move(diag));
                return;
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx, inside_fn_now);
    }
}

class HitObjectStoredInMemory : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()),
             tree.source_bytes(),
             tree,
             ctx,
             /*inside_function=*/false);
    }
};

}  // namespace

std::unique_ptr<Rule> make_hitobject_stored_in_memory() {
    return std::make_unique<HitObjectStoredInMemory>();
}

}  // namespace hlsl_clippy::rules
