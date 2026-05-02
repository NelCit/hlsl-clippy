#include "hlsl_clippy/lint.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/engine.hpp"
#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/ir.hpp"
#include "hlsl_clippy/language.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/suppress.hpp"
#include "reflection/engine.hpp"

// Phase 7 (ADR 0016): the IR engine TU is built only when
// `HLSL_CLIPPY_ENABLE_IR=ON` (the default). When OFF, `core/src/ir/engine.cpp`
// is excluded from the link and `HLSL_CLIPPY_IR_DISABLED` is defined; we
// guard the include + the dispatch block here so the orchestrator still
// builds and emits a one-shot "compiled out" diagnostic for any Stage::Ir
// rule the caller enabled.
#if !defined(HLSL_CLIPPY_IR_DISABLED)
#include "ir/engine.hpp"
#endif

#include "parser_internal.hpp"

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

/// True when `rule` is enabled under the current experimental-target
/// selection. Rules with `experimental_target() == None` are always
/// enabled; targeted rules fire only when `active` matches the rule's
/// declared target. Per ADR 0018, mismatches are silent (no diagnostic)
/// so default builds emit zero IHV-specific noise.
[[nodiscard]] bool experimental_target_enabled(const Rule& rule,
                                               ExperimentalTarget active) noexcept {
    const auto required = rule.experimental_target();
    if (required == ExperimentalTarget::None) {
        return true;
    }
    return required == active;
}

/// Iterative depth-first walk of the tree-sitter CST. Calls each AST-stage
/// rule's `on_node` for every named node in document order. Reflection-stage
/// rules are skipped here -- they fire later via `on_reflection`.
void walk(::TSNode root,
          std::span<const std::unique_ptr<Rule>> rules,
          RuleContext& ctx,
          std::string_view source_bytes,
          SourceId source_id,
          ExperimentalTarget active_target) {
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
                if (!experimental_target_enabled(*rule, active_target)) {
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

/// True when AST + CFG + IR rule dispatch is allowed for the resolved
/// source language. ADR 0020 sub-phase A (v1.3.0) returned `false` for
/// `Slang` because tree-sitter-hlsl couldn't parse Slang's language
/// extensions. ADR 0021 sub-phase B (v1.4.0) lit up tree-sitter-slang for
/// `.slang` paths via the parser dispatch in `core/src/parser.cpp`, so the
/// AST + CFG stages now run for both languages — every recognised source
/// language has a parser. Auto must already have been resolved upstream via
/// `resolve_language()`.
[[nodiscard]] bool should_dispatch_ast_stage(SourceLanguage lang) noexcept {
    (void)lang;
    return true;
}

/// True when at least one rule in `rules` has `stage() == Stage::Reflection`
/// AND is enabled under the current experimental-target selection. Used by
/// the orchestrator to short-circuit reflection-engine construction for
/// AST-only rule packs (or for packs whose only reflection rules are gated
/// behind an inactive experimental target).
[[nodiscard]] bool any_reflection_rule(std::span<const std::unique_ptr<Rule>> rules,
                                       ExperimentalTarget active_target) noexcept {
    for (const auto& rule : rules) {
        if (rule && rule->stage() == Stage::Reflection &&
            experimental_target_enabled(*rule, active_target)) {
            return true;
        }
    }
    return false;
}

/// True when at least one rule in `rules` has `stage() == Stage::ControlFlow`
/// AND is enabled under the current experimental-target selection. Used by
/// the orchestrator to short-circuit CFG-engine construction for AST-only /
/// reflection-only rule packs (ADR 0013).
[[nodiscard]] bool any_control_flow_rule(std::span<const std::unique_ptr<Rule>> rules,
                                         ExperimentalTarget active_target) noexcept {
    for (const auto& rule : rules) {
        if (rule && rule->stage() == Stage::ControlFlow &&
            experimental_target_enabled(*rule, active_target)) {
            return true;
        }
    }
    return false;
}

/// True when at least one rule in `rules` has `stage() == Stage::Ir` AND
/// is enabled under the current experimental-target selection. Used by the
/// orchestrator to short-circuit IR-engine construction for AST-only /
/// reflection-only / CFG-only rule packs (ADR 0016). When this returns
/// false the IR engine is never touched and the (eventual) DXC bridge
/// never runs.
[[nodiscard]] bool any_ir_rule(std::span<const std::unique_ptr<Rule>> rules,
                               ExperimentalTarget active_target) noexcept {
    for (const auto& rule : rules) {
        if (rule && rule->stage() == Stage::Ir &&
            experimental_target_enabled(*rule, active_target)) {
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
                                                const LintOptions& options,
                                                ExperimentalTarget active_target,
                                                SourceLanguage resolved_language,
                                                const Config* config = nullptr) {
    // ADR 0021 sub-phase B (v1.4.0) — both `.hlsl` and `.slang` paths now
    // dispatch the AST stage. `.slang` parses through tree-sitter-slang
    // (which extends tree-sitter-hlsl by reference, preserving 99% of
    // node-kinds) while `.hlsl` continues through tree-sitter-hlsl.
    // `should_dispatch_ast_stage` is kept as a hook for a future opt-out
    // surface (e.g. a Metal-only target language v2.0+); today it always
    // returns true.
    const bool dispatch_ast = should_dispatch_ast_stage(resolved_language);

    std::optional<parser::ParsedSource> parsed;
    if (dispatch_ast) {
        // ADR 0021 v1.4.x cleanup — pass the orchestrator-resolved
        // `SourceLanguage` to the parser explicitly so a config override
        // (`[lint] source-language = "slang"` on an `.hlsl`-extension path,
        // or vice versa) routes to the correct grammar at the parser layer
        // instead of being re-derived from the file extension.
        parsed = parser::parse(sources, source, resolved_language);
        if (!parsed) {
            return {};
        }
    }

    // Suppression scanning is grammar-agnostic — it walks raw UTF-8 bytes —
    // so it works equally well for `.slang` sources where we never parse.
    const SourceFile* file = sources.get(source);
    const std::string_view source_bytes = file != nullptr ? file->contents() : std::string_view{};
    const SuppressionSet suppressions = SuppressionSet::scan(source_bytes);

    RuleContext ctx{sources, source};
    ctx.set_suppressions(&suppressions);
    ctx.set_config(config);

    // ADR 0021 sub-phase B (v1.4.0) — `clippy::language-skip-ast` is no
    // longer emitted on `.slang` sources because tree-sitter-slang now
    // handles them via the parser dispatch in `core/src/parser.cpp`. The
    // diagnostic code itself is preserved (still suppressible / configurable
    // for forward compatibility) but the orchestrator never raises it under
    // sub-phase B's "every recognised language has a parser" invariant.
    // Should a future language land without a parser, this is the place to
    // emit the notice; reuse the wording in CHANGELOG / docs at that point.
    (void)resolved_language;

    // The tree view + AST root are only valid when we parsed; reflection +
    // CFG dispatch below uses these only inside `dispatch_ast`-gated blocks
    // (CFG construction needs the parsed root) or guards against nullptr
    // (Reflection rules don't touch the tree view today).
    AstTree tree_view{nullptr, nullptr, source_bytes, source};
    ::TSNode root{};
    if (dispatch_ast) {
        tree_view = AstTree{parsed->tree.get(), parsed->language, parsed->bytes, parsed->source};
        root = ts_tree_root_node(parsed->tree.get());
    }

    // ----- AST stage --------------------------------------------------------
    // Declarative pass: every rule's `on_tree` runs once with the whole tree.
    // Rules gated behind a non-matching `[experimental.target]` are skipped
    // silently so default builds emit zero IHV-specific noise (ADR 0018).
    // Skipped entirely on Slang sources (ADR 0020 sub-phase A).
    if (dispatch_ast) {
        for (const auto& rule : rules) {
            if (rule->stage() != Stage::Ast) {
                continue;
            }
            if (!experimental_target_enabled(*rule, active_target)) {
                continue;
            }
            rule->on_tree(tree_view, ctx);
        }

        // Imperative pass: the rule walker invokes every Stage::Ast rule's
        // `on_node` on each named node in document order. Reflection-stage rules
        // are skipped here (they get dispatched via `on_reflection` below).
        walk(root, rules, ctx, parsed->bytes, parsed->source, active_target);
    }

    // ----- Reflection stage -------------------------------------------------
    // Construct the engine only if at least one rule asked for reflection AND
    // the caller didn't disable reflection via options. Per ADR 0012, an
    // AST-only rule pack pays zero Slang cost. Experimental-target-gated
    // reflection rules count against the threshold only when the active
    // target matches (ADR 0018).
    //
    // ADR 0020 sub-phase A v1.3.1: reflection now runs on `.slang` sources
    // too — the bridge's virtual_path scheme was hardened so Slang's
    // native frontend can ingest `.slang` directly (the per-call
    // uniquification suffix moved off the user-visible extension into the
    // module name only; see `core/src/reflection/slang_bridge.cpp`).
    // AST + CFG + IR remain gated on `dispatch_ast` because tree-sitter-hlsl
    // still cannot parse Slang's language extensions; sub-phase B revisits.
    std::optional<ReflectionInfo> reflection_for_cfg;
    if (options.enable_reflection && any_reflection_rule(rules, active_target)) {
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
                if (rule->stage() != Stage::Reflection) {
                    continue;
                }
                if (!experimental_target_enabled(*rule, active_target)) {
                    continue;
                }
                rule->on_reflection(tree_view, reflection, ctx);
            }
            reflection_for_cfg = reflection;
        }
    }

    // ----- Control-flow stage -----------------------------------------------
    // Construct the CFG engine only if at least one rule asked for control
    // flow AND the caller didn't disable it via options (ADR 0013). The
    // engine reuses the reflection result from the same lint run when one
    // is available, so the uniformity analyzer can see resource bindings.
    //
    // We pass the already-parsed tree-sitter root + bytes via
    // `build_with_tree` so the CFG engine doesn't pay for a second
    // tree-sitter parse of the same source -- on the public corpus the
    // reparse was ~5-15% of total lint time per source, depending on
    // whether reflection was also enabled.
    //
    // ADR 0020 sub-phase A: skipped on Slang sources because the CFG is
    // built over the tree-sitter-hlsl AST. The one-shot
    // `clippy::language-skip-ast` notice already announced this.
    if (dispatch_ast && options.enable_control_flow &&
        any_control_flow_rule(rules, active_target)) {
        auto& cfg_engine = control_flow::CfgEngine::instance();
        const ReflectionInfo* reflection_ptr =
            reflection_for_cfg.has_value() ? &reflection_for_cfg.value() : nullptr;
        auto cfg_or_error = cfg_engine.build_with_tree(
            source, root, parsed->bytes, reflection_ptr, options.cfg_inlining_depth);
        if (!cfg_or_error.has_value()) {
            ctx.emit(std::move(cfg_or_error.error()));
        } else {
            const ControlFlowInfo& cfg_info = cfg_or_error.value();
            for (const auto& rule : rules) {
                if (rule->stage() != Stage::ControlFlow) {
                    continue;
                }
                if (!experimental_target_enabled(*rule, active_target)) {
                    continue;
                }
                rule->on_cfg(tree_view, cfg_info, ctx);
            }
            // Surface any per-function `clippy::cfg-skip` warnings the
            // builder collected (ERROR-tolerance path; see ADR 0013
            // §"Risks & mitigations").
            for (auto& diag : cfg_engine.take_diagnostics(source)) {
                ctx.emit(std::move(diag));
            }
        }
    }

    // ----- IR stage ---------------------------------------------------------
    // Construct the IR engine only if at least one rule asked for IR AND the
    // caller didn't disable it via options (ADR 0016). The engine reuses the
    // Slang ISession pool from ADR 0012 (a future 7a.2 will additionally call
    // `getEntryPointCode` inside the same compile path); 7a.1 ships a stub
    // engine that always returns the `clippy::ir-not-implemented` Note
    // diagnostic. Either way the routing logic is the same and is locked in
    // by tests so 7a.2 only changes the engine internals, not this dispatch.
    //
    // When the engine TU is compiled out via `HLSL_CLIPPY_ENABLE_IR=OFF`, we
    // emit a single `clippy::ir-compiled-out` Note diagnostic per source per
    // lint run so the user knows their Stage::Ir rule selection was
    // observed but no engine is available.
    //
    // ADR 0020 sub-phase A: skipped on Slang sources because IR rules
    // consume the tree-sitter AST view alongside the IR. The one-shot
    // `clippy::language-skip-ast` notice already announced this.
    if (dispatch_ast && options.enable_ir && any_ir_rule(rules, active_target)) {
#if defined(HLSL_CLIPPY_IR_DISABLED)
        Diagnostic diag;
        diag.code = std::string{"clippy::ir-compiled-out"};
        diag.severity = Severity::Note;
        diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        diag.message = std::string{
            "IR-stage rule selected but this build was configured with "
            "`HLSL_CLIPPY_ENABLE_IR=OFF` (ADR 0016). Re-build with the option "
            "ON to enable Phase 7 IR-level analysis. AST / reflection / "
            "control-flow rules are unaffected."};
        ctx.emit(std::move(diag));
#else
        const std::string profile =
            options.target_profile.has_value() ? *options.target_profile : std::string{};
        auto& ir_engine = ir::IrEngine::instance();
        auto ir_or_error = ir_engine.analyze(sources, source, profile);
        if (!ir_or_error.has_value()) {
            // Surface the engine's diagnostic (e.g. the 7a.1 not-implemented
            // notice, or in 7a.2 a real DXIL-parse failure) and skip IR rules
            // for this source. AST / reflection / CFG rules already fired.
            ctx.emit(std::move(ir_or_error.error()));
        } else {
            const IrInfo& ir_info = ir_or_error.value();
            for (const auto& rule : rules) {
                if (rule->stage() != Stage::Ir) {
                    continue;
                }
                if (!experimental_target_enabled(*rule, active_target)) {
                    continue;
                }
                rule->on_ir(tree_view, ir_info, ctx);
            }
        }
#endif
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

namespace {

/// Resolve the source language for one lint call. When the caller-supplied
/// `selected` is `Auto`, look up the source file's path via the
/// `SourceManager` and infer from its extension. The CLI / LSP both register
/// the on-disk path verbatim via `add_file()` / `add_buffer(path, ...)` so
/// the inference key is the same in both surfaces.
[[nodiscard]] SourceLanguage resolve_for_source(const SourceManager& sources,
                                                SourceId source,
                                                SourceLanguage selected) noexcept {
    if (selected != SourceLanguage::Auto) {
        return selected;
    }
    if (const SourceFile* file = sources.get(source); file != nullptr) {
        return detect_language(file->path());
    }
    return SourceLanguage::Hlsl;
}

}  // namespace

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules) {
    // No config: every experimental-target-gated rule is skipped (active
    // target = `None`). This keeps default builds free of IHV-specific
    // diagnostics even when the caller forgot to wire a `Config`.
    const auto lang = resolve_for_source(sources, source, SourceLanguage::Auto);
    return run_rules(sources, source, rules, LintOptions{}, ExperimentalTarget::None, lang);
}

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules,
                             const LintOptions& options) {
    const auto lang = resolve_for_source(sources, source, SourceLanguage::Auto);
    return run_rules(sources, source, rules, options, ExperimentalTarget::None, lang);
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

    // ADR 0020 sub-phase A: resolve the source language with the per-config
    // `[lint] source-language` override taking priority over per-file
    // extension inference. When the config selects `Auto` (the default and
    // empty-config case), the resolver falls back to inference using
    // `file_path`'s extension.
    SourceLanguage lang = config.source_language();
    if (lang == SourceLanguage::Auto) {
        if (!file_path.empty()) {
            lang = detect_language(file_path);
        } else {
            lang = resolve_for_source(sources, source, SourceLanguage::Auto);
        }
    }

    auto diagnostics =
        run_rules(sources, source, rules, options, config.experimental_target(), lang, &config);

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
