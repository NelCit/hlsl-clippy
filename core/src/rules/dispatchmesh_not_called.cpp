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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

/// Find the byte offset of the brace-balanced compound statement starting at
/// or after `from`. Returns a pair of [body-start, body-end-exclusive], with
/// body-end-exclusive pointing one past the matching `}`. If no balanced
/// block is found, both fields equal `std::string_view::npos`.
struct BraceRange {
    std::size_t lo = std::string_view::npos;
    std::size_t hi = std::string_view::npos;
};
[[nodiscard]] BraceRange find_braced_body(std::string_view text, std::size_t from) noexcept {
    BraceRange out;
    std::size_t i = from;
    while (i < text.size() && text[i] != '{') {
        ++i;
    }
    if (i >= text.size()) {
        return out;
    }
    out.lo = i;
    int depth = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                out.hi = i + 1U;
                return out;
            }
        }
    }
    out = BraceRange{};
    return out;
}

/// Textual scan: find every `[shader("amplification")]` occurrence in the
/// source, then locate the brace-balanced function body that follows and
/// check it for a `DispatchMesh` call. Resilient to the tree-sitter-hlsl
/// gap on multi-attribute function definitions (`[shader] [numthreads]`
/// combos) where the AST may not surface a clean `function_definition`
/// node. See `external/treesitter-version.md`.
void scan_textually(std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    const std::string_view shader_attr_pattern{"[shader("};
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto found = bytes.find(shader_attr_pattern, pos);
        if (found == std::string_view::npos) {
            return;
        }
        // Find the closing `)]` of the attribute.
        const auto close_paren = bytes.find(')', found);
        if (close_paren == std::string_view::npos) {
            return;
        }
        const auto attr_text = bytes.substr(found, (close_paren + 1U) - found);
        if (attr_text.find(k_amplification_tag) == std::string_view::npos) {
            pos = close_paren + 1U;
            continue;
        }
        // Locate the function body that follows.
        const BraceRange body = find_braced_body(bytes, close_paren);
        if (body.lo == std::string_view::npos) {
            pos = close_paren + 1U;
            continue;
        }
        const auto body_text = bytes.substr(body.lo, body.hi - body.lo);
        if (!body_calls_dispatch_mesh(body_text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{.lo = static_cast<std::uint32_t>(found),
                                  .hi = static_cast<std::uint32_t>(body.hi)},
            };
            diag.message = std::string{
                "amplification-shader entry point must call `DispatchMesh(...)` exactly once "
                "before returning -- the contract is required by the D3D12 mesh pipeline; "
                "missing the call is UB (deadlock on RDNA, dropped meshlets on Ada, TDR on "
                "Xe-HPG)"};
            ctx.emit(std::move(diag));
        }
        pos = body.hi;
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
        // Tree-sitter-hlsl v0.2.0 has a known gap on multi-attribute function
        // definitions (`[shader("amplification")] [numthreads(...)]` combos).
        // Use a textual scan over the source so the rule fires regardless of
        // whether the AST surfaces a clean `function_definition` node.
        scan_textually(tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_dispatchmesh_not_called() {
    return std::make_unique<DispatchmeshNotCalled>();
}

}  // namespace hlsl_clippy::rules
