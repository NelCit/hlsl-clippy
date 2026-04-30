---
status: Proposed
date: 2026-04-30
deciders: NelCit
tags: [phase-1, rule-engine, quick-fix, suppression, config, planning]
---

# Phase 1 implementation plan

## Context and Problem Statement

The `pow-const-squared` agent (`ab32e0402af2a13c7`, in flight) lands the Phase 0
+ minimum Phase 1 substrate: `SourceManager`, `Diagnostic`, `Rule` interface, a
tree-sitter parser bridge, a lint orchestration loop, Catch2 v3, and the
`hlsl-clippy lint <file>` subcommand. That work proves a single imperative rule
end-to-end on a real shader.

Phase 1's full deliverable set per `ROADMAP.md` (lines 59-66) is broader. Beyond
the seed rule, Phase 1 must ship:

1. A **declarative s-expression query helper** so subsequent rules don't each
   reinvent a `RecursiveASTVisitor`-style traversal.
2. A **quick-fix framework** (`Rewriter`, `--fix` CLI flag, conflict
   detection). Quick-fixes are the clippy comparison; they belong here, not
   Phase 5.
3. **Inline suppression** parsing for `// hlsl-clippy: allow(rule-name)`,
   line-scoped and block-scoped.
4. **`.hlsl-clippy.toml` config** for severity, includes/excludes, per-dir
   overrides.
5. **Two new end-to-end rules** with quick-fixes and doc pages:
   `redundant-saturate` and `clamp01-to-saturate`.

This ADR is the implementation plan for items 1-5. It assumes the Phase 0
agent's commit has landed and extends, rather than re-designs, what it ships.

## Decision Drivers

- **Public-header surface stays small** (ADR 0003). Phase 1 adds exactly three
  new public headers: `rewriter.hpp`, `suppress.hpp`, `config.hpp`.
- **No public dependency on tree-sitter / Slang.** TSQuery and TSNode types are
  wrapped privately; rules see neutral wrapper types.
- **C++23 idioms** (`std::expected`, `std::span`, `std::flat_map`).
- **No regressions on `pow-const-squared`.** The query helper coexists with the
  imperative path; the seed rule keeps working unchanged.

## Decision Outcome

### 1. Architecture deltas vs. Phase 0

Phase 0 ships, in `core/`: `source_manager.{hpp,cpp}`,
`diagnostic.{hpp,cpp}`, `rule.hpp`, `parser/tree_sitter_bridge.{hpp,cpp}`,
`lint.{hpp,cpp}`, and one rule (`rules/pow_const_squared.{hpp,cpp}`).

Phase 1 adds (all under `core/`):

| Public header                          | Source                  | Purpose                            |
| -------------------------------------- | ----------------------- | ---------------------------------- |
| `include/hlsl_clippy/rewriter.hpp`     | `src/rewriter.cpp`      | `Rewriter`, `TextEdit`, `Fix`      |
| `include/hlsl_clippy/suppress.hpp`     | `src/suppress.cpp`      | `SuppressionSet`, comment scanner  |
| `include/hlsl_clippy/config.hpp`       | `src/config.cpp`        | `Config`, TOML loader, resolver    |

Private (no public header):

- `src/query/query.{hpp,cpp}` — `Query`, `QueryMatch`, `QueryEngine` over
  `TSQuery`. Rules consume it via `#include "query/query.hpp"` from `core/src/`.
- `src/rules/redundant_saturate.{hpp,cpp}`,
  `src/rules/clamp01_to_saturate.{hpp,cpp}`.

CLI changes (`cli/src/main.cpp`): add `--fix` (apply machine-applicable fixes
in place) and `--config <path>` (override auto-resolution). `lint` reads
`.hlsl-clippy.toml` from the file's parent chain.

Tests (new `tests/unit/` directory): `test_query.cpp`, `test_rewriter.cpp`,
`test_suppress.cpp`, `test_config.cpp`,
`tests/unit/rules/test_redundant_saturate.cpp`,
`tests/unit/rules/test_clamp01_to_saturate.cpp`. Golden snapshots under
`tests/golden/`.

Docs: `docs/rules/redundant-saturate.md`, `docs/rules/clamp01-to-saturate.md`
(template at `docs/rules/_template.md` already exists).

### 2. Query API design

Tree-sitter ships a stable s-expression query DSL with a C API (`TSQuery`,
`TSQueryCursor`). The wrapper exposes them behind RAII types and a
callback-driven match loop.

```cpp
// core/src/query/query.hpp  (private)
namespace hlsl_clippy::query {

struct Capture { std::string_view name; TSNode node; };

struct QueryMatch {
    uint32_t pattern_index;
    std::span<const Capture> captures;
    [[nodiscard]] TSNode capture(std::string_view name) const;
};

class Query {
public:
    static std::expected<Query, Diagnostic>
        compile(const TSLanguage* lang, std::string_view pattern);
    ~Query();                  // RAII over TSQuery*; non-copy, movable.
};

class QueryEngine {
public:
    void run(const Query& q, TSTree* tree,
             std::function<void(const QueryMatch&)> cb);
};
}  // namespace hlsl_clippy::query
```

A `redundant-saturate` rule expressed declaratively:

```cpp
constexpr std::string_view kPattern = R"(
    (call_expression
        function: (identifier) @outer
        arguments: (argument_list
            (call_expression
                function: (identifier) @inner) @inner_call)) @outer_call
)";

void RedundantSaturate::on_tree(const ParsedTree& t, DiagSink& sink) {
    static const auto query = query::Query::compile(t.language(), kPattern).value();
    query::QueryEngine eng;
    eng.run(query, t.raw(), [&](const query::QueryMatch& m) {
        if (text(m.capture("outer")) != "saturate") return;
        if (text(m.capture("inner")) != "saturate") return;
        sink.emit(make_diagnostic(m.capture("outer_call"), ...));
    });
}
```

**Imperative vs. declarative.** Declarative is preferred for any rule whose
match is one tree shape: predicate is in the pattern, the engine batches
cursor iteration, patterns are testable in isolation. Imperative remains for
cross-statement state (Phase 4 control-flow rules will need it). Both paths
share the same `Rule` interface. Pattern syntax errors are caught on first
call (`compile().value()` trips immediately); CI smoke-runs every rule's
`compile()` against an empty tree.

### 3. Rewriter design

```cpp
// core/include/hlsl_clippy/rewriter.hpp
struct TextEdit { uint32_t byte_lo, byte_hi; std::string replacement; };

struct Fix {
    std::string description;
    bool        machine_applicable = true;
    std::vector<TextEdit> edits;     // disjoint within one Fix
};

class Rewriter {
public:
    std::string apply(std::string_view source,
                      std::span<const std::pair<RuleId, Fix>> fixes,
                      std::vector<Conflict>* conflicts_out = nullptr);
};
```

**Data flow.** Rules emit `Diagnostic { ..., fix: Option<Fix> }`. The driver
collects diagnostics, partitions into "with fix" / "without fix". On `--fix`,
the driver hands the with-fix list to `Rewriter::apply`, writes the result
back to disk via atomic rename, and prints a summary.

**Algorithm.** (1) Flatten all `Fix`es to `(rule_id, byte_lo, byte_hi,
replacement)` tuples. (2) Sort descending by `byte_lo`; multi-edit `Fix`es
stay grouped — if any edit conflicts, the whole `Fix` drops (atomicity). (3)
Walk pairwise; if `e[i].byte_lo < e[i-1].byte_hi`, drop the lower-priority
edit (priority = severity rank, then lexicographic `rule_id` for
determinism). (4) Apply remaining edits in reverse-byte-order onto the source
copy. (5) Conflicts emit a `clippy::fix-conflict` warn-level diagnostic.

**Idempotency.** `lint --fix` on a clean file is a no-op. Re-running `--fix`
after rules are clean must produce zero edits and a zero-byte diff. Catch2
test `test_rewriter.cpp::idempotent_after_fix` parses the rewritten buffer
with the same `RuleSet` and asserts an empty diagnostic list — catches rules
whose fix output itself matches their pattern (a real risk for
`redundant-saturate` if the fix isn't surgical).

**Conflict resolution.** Rare in Phase 1 (the two new rules don't overlap
each other or with `pow-const-squared`'s span shape). The framework still
handles it: drop lower-priority, emit conflict diagnostic. Phase 4
(`acos-without-saturate` wrapping fix may overlap `redundant-saturate`'s
removal fix) makes this load-bearing.

### 4. Suppression parser design

Tree-sitter-hlsl emits comments as nodes, but iterating the whole tree to
find suppression markers is overkill. A small forward-only scanner over the
raw source bytes is faster and decouples suppression from grammar gaps.

The scanner tracks three states: `kCode`, `kLineComment` (after `//`),
`kBlockComment` (after `/*`). String literals are skipped to avoid
`// inside a string` false matches. On entering `kLineComment`,
lookahead-match `\s*hlsl-clippy:\s*allow\s*\(`, parse rule names until `)`,
record `(rule_id, comment_line, scope)`.

Scope inference: scan forward from end-of-line for the next non-whitespace,
non-comment token. If `{`, scope is `kBlock` and runs to the matching `}`
(brace-depth tracked by the same scanner). Otherwise scope is `kLine` and
runs to the next non-blank, non-comment line.

```cpp
// core/include/hlsl_clippy/suppress.hpp
struct Suppression { std::string rule_id; uint32_t byte_lo, byte_hi; };

class SuppressionSet {
public:
    static SuppressionSet scan(std::string_view source);
    [[nodiscard]] bool suppresses(std::string_view rule_id,
                                  uint32_t byte_lo, uint32_t byte_hi) const;
private:
    // Sorted interval list per rule_id; `*` matches any.
    std::flat_map<std::string,
                  std::vector<std::pair<uint32_t, uint32_t>>> by_rule_;
};
```

**Integration.** `lint_file` builds a `SuppressionSet` once after parsing.
The driver wraps `DiagSink` so every `emit()` consults
`suppresses(rule_id, span.lo, span.hi)` and drops on hit. Suppressions are
visible under `--debug` so users can confirm an annotation actually fired.

**Edge cases.** Comma-separated lists (`allow(rule-a, rule-b)`); wildcard
`allow(*)`; trailing comment on same line as code is line-scoped; block
suppression with no following `{` falls back to line scope plus a
`clippy::malformed-suppression` warn diagnostic; nested `{` doesn't extend
the outer block.

### 5. Config file format + loader

**Library: `toml++` v3** (https://github.com/marzer/tomlplusplus, MIT,
header-only, ~9k LOC). Alternatives evaluated: `cpptoml` (unmaintained since
2021, rejected); `toml11` (acceptable fallback, slower compile, weaker
diagnostics); hand-rolled (TOML 1.0 is ~2k LOC, not worth it). Vendor as
`external/tomlplusplus/` submodule and `#include` from `config.cpp` only —
keeps compile-time impact local. Cold-build delta budget: 8s on the Linux
Clang job; if exceeded, swap to `toml11`.

**Schema.**

```toml
# .hlsl-clippy.toml
[rules]
pow-const-squared    = "warn"   # "allow" | "warn" | "deny"
redundant-saturate   = "warn"
clamp01-to-saturate  = "warn"

[includes]
paths = ["shaders/**/*.hlsl", "shaders/**/*.hlsli"]

[excludes]
paths = ["shaders/generated/**", "shaders/third_party/**"]

[[overrides]]
path  = "shaders/legacy/**"
rules = { redundant-saturate = "allow" }
```

```cpp
// core/include/hlsl_clippy/config.hpp
struct Config {
    std::flat_map<std::string, Severity> rule_severity;
    std::vector<std::string> includes, excludes;
    std::vector<RuleOverride> overrides;
    [[nodiscard]] Severity severity_for(std::string_view rule_id,
                                        std::string_view file_path) const;
};
std::expected<Config, Diagnostic>
    load_config(std::span<const std::filesystem::path> search_chain);
```

**Resolution.** (1) From the directory of the file being linted, walk
parents collecting every `.hlsl-clippy.toml`. (2) Stop at the first parent
containing `.git/` (workspace boundary). (3) Merge top-down: workspace root
first, file-local last; later wins on severity, `[[overrides]]` arrays
concatenate. (4) `--config <path>` short-circuits the search.

**Errors.** Malformed TOML surfaces as a `clippy::config` deny-level
diagnostic with line/column from toml++. CLI exits non-zero on any.

### 6. Two new rules

#### `redundant-saturate`

- **Pattern:**
  ```
  (call_expression
      function: (identifier) @outer
      arguments: (argument_list
          (call_expression
              function: (identifier) @inner) @inner_call)) @outer_call
  ```
- **Predicate:** `text(outer) == "saturate" && text(inner) == "saturate"`.
- **Fix:** single `TextEdit` replacing the outer call's range with the inner
  call's text (`byte_lo = outer_call.byte_lo`, `byte_hi = outer_call.byte_hi`,
  `replacement = source.substr(inner_call_range)`). Machine-applicable.
- **False positives:** none expected; `saturate` is a reserved HLSL intrinsic
  and shadowing it is undefined behavior. Documented limitation.
- **Fixture:** reuse `tests/fixtures/phase2/redundant.hlsl` lines 1-12 (HIT
  markers already present).

#### `clamp01-to-saturate`

- **Pattern:**
  ```
  (call_expression
      function: (identifier) @fn
      arguments: (argument_list
          (_) @x
          (number_literal) @lo
          (number_literal) @hi)) @call
  ```
- **Predicates:** `text(fn) == "clamp"`, `parse_double(text(lo)) == 0.0`,
  `parse_double(text(hi)) == 1.0`. Both bounds must be **literal numbers**;
  `clamp(x, k, m)` with non-literal bounds does not fire.
- **Fix:** `TextEdit { byte_lo = call.byte_lo, byte_hi = call.byte_hi,
  replacement = "saturate(" + text(x) + ")" }`. Machine-applicable.
- **False-positive avoidance:** parse `lo`/`hi` as `double`; accept `0`,
  `0.0`, `0.0f`, `0.f`, `0e0`, etc. — anything round-tripping to exactly
  `0.0`. Same for `1.0`. Anything else (including `0u`, `0.0001`) doesn't
  fire.
- **Fixture:** reuse `tests/fixtures/phase2/redundant.hlsl` lines 14-22.

Doc pages follow `_template.md`. The "Why it matters on a GPU" paragraph
roots both in the saturate-modifier-free instruction on RDNA / Turing
(saturate is a free output modifier; clamp is two comparisons + two
selects). Both pages carry `applicability: machine-applicable`.

### 7. Test plan

```
tests/
  unit/
    test_query.cpp           # compile, capture iteration, error paths
    test_rewriter.cpp        # disjoint, overlap, idempotent, multi-fix
    test_suppress.cpp        # line, block, wildcard, malformed
    test_config.cpp          # parse, walk-up, merge, override
    rules/
      test_redundant_saturate.cpp
      test_clamp01_to_saturate.cpp
  golden/
    redundant_saturate.{hlsl,expected}
    clamp01_to_saturate.{hlsl,expected}
```

`test_rewriter.cpp` invariants: (1) disjoint single-fix edits apply
correctly; (2) overlaps yield a conflict diagnostic + lower-priority dropped;
(3) post-fix re-lint yields zero diagnostics (idempotency); (4) multi-edit
`Fix` is atomic — one conflicting edit drops the whole `Fix`.

`test_suppress.cpp` covers line, block, wildcard, comma-list, malformed
emits its own diagnostic, and an end-to-end test where
`// hlsl-clippy: allow(redundant-saturate)` above a nested-saturate call
drops the diagnostic.

`test_config.cpp` covers TOML parse, walk-up resolution, merge ordering
(workspace → file-local), `[[overrides]]` glob matching, malformed TOML
emits a `clippy::config` diagnostic.

Golden tests: lint a fixture, capture CLI text output, diff against
`tests/golden/<name>.expected`. Update via `--update-snapshots` flag. Diff
on byte equality; CI normalizes line endings on Windows.

### 8. Implementation order

Six commits, each independently buildable and green:

1. **`feat(query): TSQuery RAII wrapper + QueryEngine`** —
   `core/src/query/`, `tests/unit/test_query.cpp`. No rule uses it yet; seed
   rule keeps imperative path.
2. **`feat(rewriter): Fix, TextEdit, Rewriter with conflict detection`** —
   `core/include/hlsl_clippy/rewriter.hpp`, `core/src/rewriter.cpp`,
   `tests/unit/test_rewriter.cpp`. Wire `--fix` flag in CLI;
   `pow-const-squared` gains its `Fix` payload here.
3. **`feat(suppress): comment scanner + SuppressionSet`** —
   `core/include/hlsl_clippy/suppress.hpp`, `core/src/suppress.cpp`,
   `tests/unit/test_suppress.cpp`. Wire into `lint_file`'s diag sink.
4. **`feat(config): toml++ vendor + Config loader`** —
   `external/tomlplusplus/` submodule, `cmake/UseTomlPlusPlus.cmake`,
   `core/include/hlsl_clippy/config.hpp`, `core/src/config.cpp`,
   `tests/unit/test_config.cpp`. Wire `--config` and walk-up in CLI.
5. **`feat(rules): redundant-saturate via Query`** —
   `core/src/rules/redundant_saturate.{hpp,cpp}`, unit + golden tests, doc
   page. First consumer of the query helper.
6. **`feat(rules): clamp01-to-saturate via Query`** — same shape as #5;
   closes Phase 1.

Commits 1-4 can each be a self-contained PR. Commits 5 and 6 should land in
the same PR. Order can be reshuffled if needed; only #5 and #6 depend on #1.

### 9. Risks and open questions

**TSQuery API stability.** Tree-sitter's query DSL has been stable since
v0.20 (vendored is v0.22). The C API needs RAII wrappers; `TSParser` /
`TSTree` patterns already exist in `tools/treesitter-smoke/main.cpp`.
`TSQueryCursor` is the new wrapper — its model is "create-once, reuse-many"
so a `QueryEngine` instance owning one cursor is the right shape. Risk: low.

**`toml++` compile-time cost.** ~9k-line single header; including from
multiple TUs blows up. Mitigation: `config.cpp` is the only includer.
Cold-build budget: 8s on Linux Clang CI. Measure first; fall back to
`toml11` or precompile a wrapper if exceeded.

**Suppression scope ambiguity for compound statements.** A
`// hlsl-clippy: allow(rule)` above a four-line fluent chain — is "next
statement-bearing line" the first line, or the whole chain? Phase 1 chooses
first-line (matches Rust `#[allow(...)]` placement); rule firings on lines
2-4 won't be suppressed. Documented; revisit if it bites.

**Multiple `.hlsl-clippy.toml` resolution.** Closer-to-file wins on severity;
`[[overrides]]` arrays concatenate (deeper files can add but not delete
inherited overrides). Open: should there be a `[meta] inherit = false`
opt-out? Defer to Phase 5 (LSP multi-root) where it actually matters.

**Tree-sitter-hlsl grammar gaps.** Known `cbuffer X : register(b0)` ERROR
node (ROADMAP open question). The comment scanner is grammar-agnostic, so
suppression is unaffected. The two new rules match inside function bodies,
clear of the cbuffer-binding gap. Verified safe.

**Golden-test brittleness.** Snapshot tests can break on cosmetic formatter
changes. Mitigation: keep the text-formatter contract narrow (single test
file pins it); CI normalizes line endings (autocrlf foot-gun on Windows).

## Confirmation

When Phase 1 closes:

- `core/include/hlsl_clippy/` contains exactly four headers: `version.hpp`,
  `rewriter.hpp`, `suppress.hpp`, `config.hpp` (plus what Phase 0 ships:
  `source_manager.hpp`, `diagnostic.hpp`, `rule.hpp`).
- `hlsl-clippy lint --fix tests/fixtures/phase2/redundant.hlsl` rewrites
  every HIT line; re-running with `--fix` is a no-op.
- `// hlsl-clippy: allow(redundant-saturate)` above a nested saturate call
  drops the diagnostic.
- A `.hlsl-clippy.toml` with `redundant-saturate = "allow"` drops every
  `redundant-saturate` diagnostic project-wide.
- Five Catch2 unit-test files plus two golden tests pass on Windows + Linux
  (ADR 0005 matrix).
- Two rule doc pages live under `docs/rules/` per `_template.md`.

## Pros and Cons of the Options

The major axis-level alternatives are covered inline. The two-rule slate
itself (`redundant-saturate` + `clamp01-to-saturate`) is fixed by ROADMAP
Phase 1 line 66 and ADR 0007.

## Links

- ROADMAP.md "Phase 1" (lines 59-66).
- `docs/architecture.md` (rule engine, diagnostic shape).
- `docs/rules/_template.md`, `docs/rules/pow-const-squared.md`.
- `tests/fixtures/phase2/redundant.hlsl` (HIT markers for both new rules).
- ADR 0002 (tree-sitter-hlsl, query language stability).
- ADR 0003 (module decomposition — public-header-surface constraint).
- ADR 0004 (C++23 — `std::expected`, `std::flat_map`).
- ADR 0005 (CI/CD — Catch2 v3, warning-flag boundary).
- ADR 0007 (rule-pack expansion — both rules pre-listed under Phase 2).
- `tools/treesitter-smoke/main.cpp` (RAII pattern reference for TSQuery).
