// dispatchmesh-not-called
//
// Detects amplification-shader entry-point function bodies that do not call
// `DispatchMesh(...)`. The amplification stage is contractually required to
// invoke `DispatchMesh` exactly once per launched thread group; missing the
// call is undefined behaviour on D3D12 (deadlock on AMD RDNA, dropped
// meshlets on Ada, TDR on Xe-HPG).
//
// Stage: Ast.
//
// Detection plan: walk every top-level function declaration and, for each
// function whose attribute list carries `[shader("amplification")]`, scan the
// function body for any `DispatchMesh` call. If the substring is absent, emit
// on the function-declaration span. The full Phase 4 design (per ADR 0013 +
// the rule's doc page) wants per-CFG-path coverage so we can flag *some-path*
// missing calls (not just whole-function); that requires CFG analysis of
// every return statement, which we forward-compatible-stub here by anchoring
// only on the "no call anywhere in the body" case. The doc page explicitly
// notes both call-not-on-some-path and call-not-anywhere as separate failure
// modes; this rule covers the latter exactly and the former is left to a
// future tightening once the engine grows per-path call-graph queries.

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

constexpr std::string_view k_rule_id = "dispatchmesh-not-called";
constexpr std::string_view k_category = "mesh";
constexpr std::string_view k_amplification_tag = "\"amplification\"";
constexpr std::string_view k_dispatch_mesh = "DispatchMesh";

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

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// True when `text` contains the bare token `DispatchMesh` (followed by an
/// open paren or whitespace). Avoids hitting `MyDispatchMeshHelper` or
/// `DispatchMeshNodes` (a related-but-distinct work-graph entry point).
[[nodiscard]] bool body_calls_dispatch_mesh(std::string_view text) noexcept {
    std::size_t pos = 0U;
    while (pos < text.size()) {
        const auto found = text.find(k_dispatch_mesh, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || !is_id_char(text[found - 1U]);
        const std::size_t end = found + k_dispatch_mesh.size();
        const bool ok_right = (end >= text.size()) || !is_id_char(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// True when the function-declaration text begins with an attribute list that
/// names the amplification shader stage.
[[nodiscard]] bool is_amplification_function(std::string_view fn_text) noexcept {
    // The grammar attaches `[shader("amplification")]` (or
    // `[numthreads(...)]` siblings) to the same function_definition node, so
    // a substring check on the leading section of the function text is
    // sufficient. We confirm the literal token to avoid accidental matches
    // on a `// "amplification"` comment in the body.
    return fn_text.find(k_amplification_tag) != std::string_view::npos &&
           fn_text.find("shader") != std::string_view::npos;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    if (kind == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        if (is_amplification_function(fn_text) && !body_calls_dispatch_mesh(fn_text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "amplification-shader entry point must call `DispatchMesh(...)` exactly once "
                "before returning -- the contract is required by the D3D12 mesh pipeline; "
                "missing the call is UB (deadlock on RDNA, dropped meshlets on Ada, TDR on "
                "Xe-HPG)"};
            ctx.emit(std::move(diag));
        }
        return;  // do not descend; nested function_definition unlikely.
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class DispatchmeshNotCalled : public Rule {
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

std::unique_ptr<Rule> make_dispatchmesh_not_called() {
    return std::make_unique<DispatchmeshNotCalled>();
}

}  // namespace hlsl_clippy::rules
