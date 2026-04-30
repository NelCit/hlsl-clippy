---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [parser, architecture, phase-0, phase-1]
---

# Parser — tree-sitter-hlsl

## Context and Problem Statement

`hlsl-clippy` rules need an AST: spans for diagnostics, syntactic match patterns, control-flow recovery on partial input. Slang's public API exposes compilation + reflection but **not** AST nodes — its AST is internal C++. So a separate parser is mandatory regardless of compiler choice.

The parser must:

1. Produce stable byte-offset spans we can hand to a diagnostic renderer and to a source rewriter.
2. Recover gracefully from partial / malformed input (the LSP needs this; the CLI on a half-saved buffer needs this).
3. Track comment trivia so inline suppressions (`// hlsl-clippy: allow(rule-name)`) work without retroactive lookups.
4. Be cheap enough to vendor — we will patch it.
5. Keep public headers free of backend-specific node types (see ADR 0003).

## Decision Drivers

- **Backend isolation.** No `tree_sitter/api.h` in `include/hlslc/`. Same constraint we apply to Slang headers.
- **Patchability.** HLSL is a moving target (work graphs, cooperative vectors, new SM 6.x intrinsics). The parser will need patches; the patch surface must be tractable.
- **Error recovery.** Tree-sitter's incremental + GLR-style recovery is a real asset for LSP and partial-buffer linting.
- **Distribution.** Distros don't ship tree-sitter-hlsl; whatever we choose must vendor cleanly.

## Considered Options

1. **tree-sitter-hlsl + tree-sitter runtime, vendored as submodules.** Small C grammar; mature runtime; query DSL for declarative pattern matching.
2. **Hand-rolled recursive-descent parser.** ~2k LOC for the subset we need; full control over node shape and span representation.
3. **Slang internal AST headers.** Reach into Slang's C++ AST. Same family of pain as DXC — not a public API; ABI churn per release.

## Decision Outcome

Chosen option: **tree-sitter-hlsl**, vendored under `third_party/tree-sitter-hlsl/` and `third_party/tree-sitter/` as git submodules, built as `OBJECT` libraries linked statically into a private `hlslc_parser` lib. Public API exposes `(SourceId, byte-lo, byte-hi)` spans only — never `TSNode`.

Where the grammar is incomplete, we patch upstream. Worst-case fallback: replace with a hand-rolled parser for the subset we need. We do not pursue the Slang-internal-AST path.

### Consequences

Good:

- Error-recovering parser out of the box; LSP and partial-buffer linting work without extra machinery.
- Tree-sitter queries (s-expression DSL) make the common syntactic-match rule case declarative — registry glue is one file plus one query string.
- Small C runtime; vendoring cost is tiny.
- Comment trivia is reachable through the parser bridge from day one — no retroactive comment lookups for suppression handling.

Bad:

- **Confirmed grammar gap (v0.2.0): `cbuffer X : register(b0) { ... }` does not parse.** The published grammar omits the explicit register-binding suffix on `cbuffer` declarations and produces an `ERROR` node, breaking any rule that needs to identify the cbuffer's binding slot. Mitigation: patch upstream and vendor the patched grammar; until then, fall back to a regex-style binding extraction or to Slang reflection's binding info on the resource by name.
- More general grammar gaps around modern HLSL (templates, work-graph attributes, some SM 6.x features). Tracked as an open question in `ROADMAP.md`.
- Tree-sitter byte offsets and Slang `SourceLoc`s come from different source managers; we own the bridge (precomputed line-offset tables per `SourceId`, fed once at file load).
- We carry vendoring + patching weight forever. The tree-sitter-hlsl repo's bus factor is small.

### Confirmation

- A Phase 0 smoke test parses an HLSL fixture and walks the tree. Listed in ROADMAP Phase 0.
- CI grep enforces no `tree_sitter/api.h` (or any `tree_sitter/*.h`) under `include/hlslc/`.
- Tracked grammar gaps live in `ROADMAP.md` "Open questions"; each new gap is filed there before patching.

## Pros and Cons of the Options

### tree-sitter-hlsl (chosen)

- Good: error-recovering, incremental, well-suited to LSP.
- Good: small grammar, easy to patch.
- Good: queries make most syntactic rules a single declarative spec.
- Bad: grammar incomplete on modern HLSL — confirmed register-suffix gap, unconfirmed gaps elsewhere.
- Bad: byte-offset bridge to Slang reflection is ours to maintain.

### Hand-rolled recursive-descent parser

- Good: full control over node shape, span representation, recovery strategy.
- Good: one less submodule.
- Bad: ~2k LOC of HLSL parsing is real work, and it's the work we'd be doing instead of writing rules.
- Bad: no community shares the maintenance load.
- Bad: parking lot for "just one more HLSL feature" maintenance forever.
- Kept as the worst-case fallback if tree-sitter-hlsl proves unmaintainable.

### Slang internal AST headers

- Good: ground-truth AST; same one Slang's compiler walks.
- Bad: not a public API. ABI churn every Slang release.
- Bad: pulls Slang's C++ headers into every rule TU — destroys compile times and isolation.
- Bad: Slang's AST shape is tuned for the compiler, not for tooling — the tooling-friendly view is what tree-sitter would give us anyway.

## Links

- Verbatim research: `_research/architecture-review.md` §2 (external dependencies) — and the freshly-confirmed grammar gap on `cbuffer : register(...)`.
- Related: ADR 0001 (Slang), ADR 0003 (module isolation of parser/semantic backends), ROADMAP "Open questions".
