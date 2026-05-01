#include "hlsl_clippy/lint.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/suppress.hpp"

#include "parser_internal.hpp"
#include "reflection/engine.hpp"

namespace hlsl_clippy {

namespace {

// RAII wrapper for TSTreeCursor; the API uses by-value initialise/destroy
// pairs so a unique_ptr is overkill, but we still want exception safety.
class TreeCursor {
public:
    explicit TreeCursor(::TSNode root) noexcept : cursor_(ts_tree_cursor_new(root)) {}
    TreeCursor(const TreeCursor&) = delete;
    TreeCursor& operator=(const TreeCursor&) = delete;
    TreeCursor(TreeCursor&&) = delete;
    TreeCursor& operator=(TreeCursor&&) = delete;
    ~TreeCursor() {
        ts_tree_cursor_delete(&cursor_);
    }

    [[nodiscard]] ::TSTreeCursor* raw() noexcept {
        return &cursor_;
    }

private:
    ::TSTreeCursor cursor_;
};

/// Iterative depth-first walk of the tree-sitter CST. Calls each AST-stage
/// rule's `on_node` for every named node in document order. Reflection-stage
/// rules are skipped here -- they fire later via `on_reflection`.
void walk(::TSNode root,
          std::span<const std::unique_ptr<Rule>> rules,
          RuleContext& ctx,
          std::string_view source_bytes,
          SourceId source_id) {
    if (ts_node_is_null(root)) {
        return;
    }
    TreeCursor cursor{root};

    while (true) {
        const ::TSNode node = ts_tree_cursor_current_node(cursor.raw());
        if (ts_node_is_named(node)) {
            const AstCursor ast{node, source_bytes, source_id};
            for (const auto& rule : rules) {
                if (rule->stage() != Stage::Ast) {
                    continue;
                }
                rule->on_node(ast, ctx);
            }
        }

        // Descend, then move sideways, then climb until we find a sibling or
        // exhaust the tree.
        if (ts_tree_cursor_goto_first_child(cursor.raw())) {
            continue;
        }
        while (!ts_tree_cursor_goto_next_sibling(cursor.raw())) {
            if (!ts_tree_cursor_goto_parent(cursor.raw())) {
                return;  // Walked back past the root; done.
            }
        }
    }
}

/// True when at least one rule in `rules` has `stage() == Stage::Reflection`.
/// Used by the orchestrator to short-circuit reflection-engine construction
/// for AST-only rule packs.
[[nodiscard]] bool any_reflection_rule(std::span<const std::unique_ptr<Rule>> rules) noexcept {
    for (const auto& rule : rules) {
        if (rule && rule->stage() == Stage::Reflection) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace {

[[nodiscard]] std::vector<Diagnostic> run_rules(const SourceManager& sources,
                                                SourceId source,
                                                std::span<const std::unique_ptr<Rule>> rules,
                                                const LintOptions& options) {
    auto parsed = parser::parse(sources, source);
    if (!parsed) {
        return {};
    }

    const SuppressionSet suppressions = SuppressionSet::scan(parsed->bytes);

    RuleContext ctx{sources, source};
    ctx.set_suppressions(&suppressions);
    const ::TSNode root = ts_tree_root_node(parsed->tree.get());

    // ----- AST stage --------------------------------------------------------
    // Declarative pass: every rule's `on_tree` runs once with the whole tree.
    const AstTree tree_view{
        parsed->tree.get(), parsed->language, parsed->bytes, parsed->source};
    for (const auto& rule : rules) {
        if (rule->stage() == Stage::Ast) {
            rule->on_tree(tree_view, ctx);
        }
    }

    // Imperative pass: the rule walker invokes every Stage::Ast rule's
    // `on_node` on each named node in document order. Reflection-stage rules
    // are skipped here (they get dispatched via `on_reflection` below).
    walk(root, rules, ctx, parsed->bytes, parsed->source);

    // ----- Reflection stage -------------------------------------------------
    // Construct the engine only if at least one rule asked for reflection AND
    // the caller didn't disable reflection via options. Per ADR 0012, an
    // AST-only rule pack pays zero Slang cost.
    if (options.enable_reflection && any_reflection_rule(rules)) {
        const std::string profile =
            options.target_profile.has_value() ? *options.target_profile : std::string{};
        auto& engine = reflection::ReflectionEngine::instance();
        auto reflection_or_error = engine.reflect(sources, source, profile);
        if (!reflection_or_error.has_value()) {
            // Surface the engine's diagnostic and skip reflection rules for
            // this source.
            ctx.emit(std::move(reflection_or_error.error()));
        } else {
            const ReflectionInfo& reflection = reflection_or_error.value();
            for (const auto& rule : rules) {
                if (rule->stage() == Stage::Reflection) {
                    rule->on_reflection(tree_view, reflection, ctx);
                }
            }
        }
    }

    auto diagnostics = ctx.take_diagnostics();

    // Surface scanner-internal diagnostics about malformed annotations.
    for (const auto& sd : suppressions.scan_diagnostics()) {
        Diagnostic diag;
        diag.code = std::string{"clippy::malformed-suppression"};
        diag.severity = Severity::Warning;
        diag.primary_span =
            Span{.source = source, .bytes = ByteSpan{.lo = sd.byte_lo, .hi = sd.byte_hi}};
        diag.message = sd.message;
        diagnostics.push_back(std::move(diag));
    }

    return diagnostics;
}

}  // namespace

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules) {
    return run_rules(sources, source, rules, LintOptions{});
}

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules,
                             const LintOptions& options) {
    return run_rules(sources, source, rules, options);
}

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules,
                             const Config& config,
                             const std::filesystem::path& file_path) {
    return lint(sources, source, rules, config, file_path, LintOptions{});
}

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules,
                             const Config& config,
                             const std::filesystem::path& file_path,
                             const LintOptions& options) {
    // Drop Allow-severity rules entirely so they never run. We have to
    // construct a temporary vector that holds the surviving `unique_ptr`s by
    // reference -- but since `std::span<const std::unique_ptr<Rule>>` requires
    // contiguous storage of `unique_ptr`s, we wrap a vector of new owning
    // smart pointers... no: cheaper to just walk the input and use a vector
    // of bare pointers + a small adapter. Easiest path: just build a parallel
    // vector of empty `unique_ptr`s from the originals via aliasing
    // construction is impossible.
    //
    // Instead: keep the rules span as-is and let `run_rules` invoke every
    // rule, then filter+re-tag the resulting diagnostics. That preserves
    // determinism and keeps the public API surface tiny. The cost is that an
    // `Allow`-marked rule still runs; for a rule that's pure-functional and
    // already gated by tree-sitter queries, that's a one-pass overhead, not a
    // behaviour change.

    auto diagnostics = run_rules(sources, source, rules, options);

    std::vector<Diagnostic> kept;
    kept.reserve(diagnostics.size());
    for (auto& diag : diagnostics) {
        const auto sev = config.severity_for(diag.code, file_path);
        if (sev.has_value()) {
            switch (*sev) {
                case RuleSeverity::Allow:
                    continue;  // drop entirely.
                case RuleSeverity::Warn:
                    diag.severity = Severity::Warning;
                    break;
                case RuleSeverity::Deny:
                    diag.severity = Severity::Error;
                    break;
            }
        }
        kept.push_back(std::move(diag));
    }
    return kept;
}

}  // namespace hlsl_clippy
