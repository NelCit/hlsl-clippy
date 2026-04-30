#pragma once

#include <string_view>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {

class SuppressionSet;

/// Pipeline stage at which a rule's hook fires. Phase 0 only ships `Ast`;
/// reflection-aware rules will introduce additional stages later.
enum class Stage {
    Ast,
};

/// Forward declaration of the tree-sitter node wrapper. The full definition
/// lives in `core/src/parser_internal.hpp` and is not visible to public-header
/// consumers (CLI, future LSP). Rules cast through this opaque wrapper.
class AstCursor;

/// Forward declaration of the tree-sitter tree wrapper. Used by rules that
/// drive a TSQuery match loop (the declarative path).
class AstTree;

/// Context passed to rule hooks. Owns the diagnostic sink for the in-progress
/// lint pass and exposes the source under analysis.
class RuleContext {
public:
    RuleContext(const SourceManager& sources, SourceId source) noexcept
        : sources_(&sources), source_(source) {}

    [[nodiscard]] const SourceManager& sources() const noexcept {
        return *sources_;
    }
    [[nodiscard]] SourceId source() const noexcept {
        return source_;
    }

    /// Install a suppression filter. Diagnostics emitted via `emit()` whose
    /// span intersects an active suppression are dropped silently. The pointer
    /// is borrowed and must outlive the `RuleContext`.
    void set_suppressions(const SuppressionSet* suppressions) noexcept {
        suppressions_ = suppressions;
    }

    /// Append a diagnostic to the current pass. Diagnostics matching an
    /// active inline-suppression are dropped silently.
    void emit(Diagnostic diag);

    /// Steal the accumulated diagnostics. Called by the lint orchestrator.
    [[nodiscard]] std::vector<Diagnostic> take_diagnostics() noexcept;

private:
    const SourceManager* sources_;
    SourceId source_;
    const SuppressionSet* suppressions_ = nullptr;
    std::vector<Diagnostic> diagnostics_;
};

/// Abstract interface every rule implements.
///
/// Phase 0 keeps the surface minimal: each rule announces its identity and
/// implements `on_node` for the syntactic match. Phase 1 will layer
/// declarative tree-sitter queries on top of this interface.
class Rule {
public:
    Rule() = default;
    Rule(const Rule&) = delete;
    Rule& operator=(const Rule&) = delete;
    Rule(Rule&&) = delete;
    Rule& operator=(Rule&&) = delete;
    virtual ~Rule() = default;

    /// Stable identifier, e.g. `"pow-const-squared"`. Matches the diagnostic
    /// code emitted by the rule.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /// Category tag, e.g. `"math"`. Used by the rule catalog and config.
    [[nodiscard]] virtual std::string_view category() const noexcept = 0;

    /// Stage at which the rule fires.
    [[nodiscard]] virtual Stage stage() const noexcept {
        return Stage::Ast;
    }

    /// Visit one AST node. Called by the lint orchestrator for every named
    /// node in document order. Default implementation does nothing.
    virtual void on_node(const AstCursor& cursor, RuleContext& ctx);

    /// Whole-tree hook called once per parsed source. Declarative rules use
    /// this to drive a TSQuery match loop in one shot rather than walking
    /// every named node imperatively. Default implementation does nothing.
    virtual void on_tree(const AstTree& tree, RuleContext& ctx);
};

}  // namespace hlsl_clippy
