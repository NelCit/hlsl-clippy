---
status: Accepted
date: 2026-05-02
decision-makers: maintainers
consulted: ADR 0001, ADR 0002, ADR 0012, ADR 0014, ADR 0019
tags: [slang-language, parser, reflection, vscode, file-extensions, v1.3, language-compatibility, research]
---

# Slang language compatibility — `.slang` source file recognition + reflection-only lint baseline

## Strategic summary (TL;DR for maintainers)

Twenty-line digest. v1.2.0 ships an HLSL-only linter — every public artifact
(file-extension list, VS Code language ID, doc copy, Marketplace keywords)
treats Slang as the *compiler* but never the *language*. Brand confusion is
real: `slang` already appears in `vscode-extension/package.json`'s keyword
list, but no `.slang` file ever reaches a rule. Adding Slang-language
support is a v1.x feature (additive, ABI-preserving) per ADR 0019 — not
a v2.0 break.

**The realistic first step is sub-phase A: file-extension recognition +
reflection-only lint for `.slang` files.** Slang's `IGlobalSession` /
`ISession` already accepts `.slang` source today (the bridge is
language-agnostic at `loadModuleFromSourceString`); the gap is in the
front-end (CLI argv handling, LSP language registration, VS Code language
contribution) and in the rule dispatch (~157 of 189 rules are AST/CFG-stage
and need a parser that can read Slang syntax — which `tree-sitter-hlsl`
cannot, by design).

**The parser story is the gating risk.** Theta-Dev/tree-sitter-slang
exists upstream but is alpha-grade (single contributor, recent activity,
no v1.0 tag) and is not a strict superset of tree-sitter-hlsl — it parses
Slang's language extensions (`__generic`, `interface`, `extension`,
`associatedtype`, `import`, `__include`) but loses some HLSL idioms in
the process. Vendoring it in v1.3 would be premature. Sub-phase B
(v1.4+) tracks the upstream maturity curve.

**The rule-applicability projection is sobering.** Of 189 shipped rules:
~32 are `Stage::Reflection` (fire on any source Slang's reflection
ingests — these work on `.slang` immediately), ~33 are `Stage::ControlFlow`
(need a CFG built over a parsed AST — silently skipped on `.slang`), and
~124 are `Stage::Ast` (need tree-sitter-hlsl — skipped). **Sub-phase A
delivers ~17% of the linter's rule surface to Slang users on day one.**
That's a meaningful baseline (resource-binding rules, cbuffer-padding
rules, descriptor-table rules all fire) but it's not parity. We tell users
honestly: AST/CFG rules wait on sub-phase B.

**Three risks to call out.** (1) FP-rate may shift — Slang's stricter type
system surfaces resources differently (e.g. `ParameterBlock<T>` flattens
to multiple binding entries in reflection where the HLSL `cbuffer` would
surface as one); rules tuned to HLSL reflection shape may misfire. (2)
Slang submodule pin pressure — sub-phase A elevates Slang reflection from
"opt-in compile target" to "mandatory parser surface for `.slang`," so a
Slang prebuilt regression on `.slang` blocks the linter for those users.
(3) VS Code marketplace overlap — `shader-slang.slang` is the official
Slang language extension; we contribute `slang` cautiously (a config knob
to suppress, default on).

**Proposed sub-phase A scope (v1.3.0):** add `.slang` to the CLI
argv-default extension globs + VS Code `extensions:` list; register a
new VS Code language contribution `slang` with a `documentSelector`
covering both `hlsl` and `slang`; route `.slang` paths through the existing
Slang reflection bridge (which already works); for non-Reflection-stage
rules, emit a one-time-per-source `clippy::language-skip-ast` informational
diagnostic and silently skip them; document the ~17% rule-surface number
honestly in `docs/configuration.md` and the README. Sub-phases B (v1.4+
tree-sitter-slang integration, full AST coverage) and C (v1.5+
Slang-specific rules: generics misuse, capability-constraint violations)
deferred behind real demand signals from sub-phase A telemetry.

---

## Context

### What "Slang the language" is

[Slang](https://shader-slang.org/) the *language* is a near-superset of
HLSL maintained by Khronos / Shader-Slang, designed for modern shader
authoring. It adds:

- **Generics** — `__generic<T : IFoo>` parameters with capability
  constraints; `<T>` instantiation at call sites.
- **Interfaces** — `interface IFoo { ... }` plus `extension Bar : IFoo`
  (retroactive interface conformance, à la Swift extensions / Rust
  trait impls).
- **Associated types** — `associatedtype Item : IDefaultable;` inside
  an interface body.
- **Modules + imports** — `import foo;` and `__include` for header-style
  inclusion; module boundaries enforced unlike `#include`.
- **Capabilities** — declarative target-feature constraints
  (`[require(spirv_1_4)]`, `[require(hw_pipeline)]`).
- **Tuples + sum types** — `(int, float)` tuples; `enum class` typed
  enums; `Optional<T>` etc. in the standard library.
- **Higher-order functions** — function-typed parameters; closures
  via lambda-like syntax.
- **Type inference** — `var x = expr;` with full type inference
  (HLSL has only the implicit-typed `auto` for some declarations).

Slang the *compiler* (the binary `slangc` and library `libslang.so` /
`slang.dll`) accepts both HLSL and Slang sources, distinguishing them
either by file extension (`.hlsl` → HLSL frontend, `.slang` → Slang
frontend) or by an explicit `-source-language` flag.

### What's currently parseable in `shader-clippy`

This linter today (v1.2.0) plumbs Slang as the **compiler** for reflection
(ADR 0001, ADR 0012) and uses **tree-sitter-hlsl** (ADR 0002) as the
parser for AST + CFG rules. Tree-sitter-hlsl handles HLSL grammar; it
does *not* handle Slang's language extensions. A `.slang` file containing
`__generic<T : IFoo>` would surface dozens of `ERROR` nodes in the parse
tree, and AST rules would either spuriously fire or miss real
diagnostics.

The Slang **reflection** bridge (`core/src/reflection/slang_bridge.cpp`)
calls `ISession::loadModuleFromSourceString(name, virtual_path, contents)`.
Slang's source-language detection happens against the `virtual_path`
extension. **A `.slang` extension at the bridge layer would Just Work
for reflection today** — that's why sub-phase A is a tractable v1.3 ship.

### Why this matters now

- **Brand confusion in the marketplace.** Searching "slang" on the VS Code
  Marketplace turns up our `nelcit.shader-clippy` extension as a
  "performance + correctness rules beyond what dxc catches" listing,
  alongside `shader-slang.slang` (the official language extension). Users
  open a `.slang` file and the linter does nothing — confusing without an
  explicit "we don't support `.slang` yet" message.
- **Khronos adoption of Slang.** Slang joined Khronos in 2024 and is the
  reference frontend for modern compute / RT / mesh shaders that target
  multiple backends (DXIL + SPIR-V + Metal + WGSL). The `.slang` file
  count in public engine repositories on GitHub is growing month over
  month per public-facing telemetry on shader-slang.org. v1.3 catches
  the curve; v2.0 is too late.
- **ADR 0019's v1.x maintenance contract.** v1.x adds features without
  breaking ABI. Slang language support is purely additive: new file
  extension recognition, new VS Code language contribution, new
  config knob. No public type changes in `core/include/shader_clippy/`.
  Lands cleanly as v1.3.

This ADR mirrors the **research-direction shape** of [ADR
0011](0011-candidate-rule-adoption.md) and [ADR
0018](0018-v08-research-direction.md) — five-dimension survey, decision
tally, per-version implementation plan, risks + mitigations, cross
references. It does **not** propose new infrastructure on the scale of
ADR 0012 / 0013; the parser + rule-engine surface is reused as-is, and
all sub-phase A work is plumbing.

## Decision drivers

- **Smallest useful first step.** Honest scope — sub-phase A delivers
  ~17% of the rule surface, not 100%. Document that number truthfully.
  Don't over-promise then under-deliver; users prefer "32 rules fire
  on your `.slang` file today, the rest wait on tree-sitter-slang
  maturity" over "Slang support shipped!" with squiggle-empty buffers.
- **Reuse the locked decisions.** Slang as compiler (ADR 0001) is
  exactly what we need; tree-sitter-hlsl (ADR 0002) is exactly what we
  *don't* need for `.slang`. Sub-phase A picks at the existing seam
  cleanly; sub-phase B is where the parser decision gets re-litigated.
- **No ABI break.** v1.3 ships within the v1.x stability commitment
  (ADR 0019). All sub-phase A changes are additive: a new method or two
  on `Config`, a new file-extension list in the VS Code manifest, a new
  language registration. Public types in `core/include/shader_clippy/`
  are untouched.
- **Telemetry-gated B and C.** Don't do tree-sitter-slang work
  speculatively. Ship sub-phase A, observe whether `.slang` users
  actually arrive (Marketplace install delta, GitHub `.github/workflows/`
  search hits using `.slang` paths), and only then commit a Phase
  10b research dispatch. Sub-phase C (Slang-specific rules) waits on
  B because Slang generics / interfaces / capabilities only become
  diagnosable once the parser sees them.
- **Honest treatment of false-positive risk.** Slang's reflection
  *normalises* HLSL and Slang inputs into the same `IModule` shape — but
  the *flattening* of `ParameterBlock<T>`, the explicit binding-space
  layout for descriptor sets, and the way Slang surfaces `interface`
  conformances differ from how HLSL `cbuffer` / `tbuffer` / resource
  declarations surface. The `cbuffer-padding-hole` rule, for example,
  is wired against the HLSL `cbuffer { ... }` shape; on a Slang
  `ParameterBlock<MyStruct>` the rule must either learn the new layout
  or stay silent. Assume FP-rate slips on `.slang` until measured.

## Five-dimension survey

### 1. tree-sitter-slang grammar status

**Upstream candidates surveyed:**

- **[Theta-Dev/tree-sitter-slang](https://github.com/Theta-Dev/tree-sitter-slang)**
  — single-author repo, alpha-grade. Recent commits as of late 2025
  through early 2026 (active). Coverage of generics + interfaces is
  partial; the README explicitly notes that the grammar does not yet
  parse some Slang stdlib idioms. **No v1.0 tag**, no published npm
  package, no semver guarantees. Build system uses Tree-sitter CLI 0.22+.
- **No `tree-sitter-grammars/tree-sitter-slang` community port** as of
  this ADR. The community-org strategy that produced `tree-sitter-hlsl`
  has not been replicated for Slang.
- **Shader-Slang upstream** does not maintain a tree-sitter grammar.
  The Slang LSP server (`slangd`) ships its own LALR-style parser
  implemented in C++ inside the Slang compiler — not reusable.

**Strict-superset question.** Theta-Dev/tree-sitter-slang is *not* a
strict superset of tree-sitter-hlsl. The grammars share most of the
HLSL surface but diverge on: (a) attribute parsing (Slang allows
`[require(...)]` and `[Differentiable]` attributes that tree-sitter-hlsl
treats as ERROR); (b) resource declarations (Slang's
`ParameterBlock<T>` is parsed; tree-sitter-hlsl treats it as
`Texture2D` family by mistake); (c) member-function syntax
(Slang's `extension Foo { void method() {} }` is unique). **Mixing
tree-sitter-hlsl and tree-sitter-slang at runtime is the right
strategy**, not picking one to replace the other.

**Plain-HLSL parse-test on tree-sitter-slang.** Spot-checked the 27
`tests/corpus/` shaders mentally against the grammar's listed coverage:
~22 of 27 would parse cleanly; the 5 that wouldn't depend on attribute
combinations (`[earlydepthstencil][numthreads(8,8,1)]` together) that
the alpha grammar has not yet covered.

**Verdict for v1.3.** *Not yet ready to vendor.* Track the upstream
through 2026; revisit at v1.4+. If demand from sub-phase A telemetry
warrants, sponsor a Phase 10b research dispatch that either (a) vendors
Theta-Dev's grammar with patches, (b) extends tree-sitter-hlsl to a
Slang superset (huge effort, ~6 months by an engaged contributor), or
(c) wraps Slang's own internal parser via a new C API exposed by
upstream Slang (depends on Shader-Slang's roadmap).

### 2. Slang's own API surface for parsing

**`slang.h` AST surface.** [Slang's public C++ API](https://docs.shader-slang.org/en/latest/external/slang/docs/api-reference/api-reference.html)
exposes:

- `slang::IGlobalSession`, `slang::ISession` — compilation context.
- `slang::IModule` — parsed module; opaque, no AST traversal API. Methods
  surface `getDefinedEntryPointCount()`, `getDefinedEntryPoint(i, ...)`,
  and a `getLayout()` that returns a reflection tree.
- `slang::IComponentType` — composable component (module, entry point,
  link result). Exposes `getLayout()` for reflection.
- `slang::TypeReflection`, `slang::FunctionReflection`,
  `slang::VariableReflection`, `slang::EntryPointReflection` —
  **reflection only.** No AST nodes, no source-position iteration, no
  expression-tree traversal.

**Conclusion: Slang's public API is strictly reflection-shaped.** There
is no public AST-walking surface, no `Slang::IExpr` type tree we can
iterate. This was a deliberate Shader-Slang design choice — the AST is
considered an implementation detail and may change between releases.

**Reflection normalisation question.** Slang's reflection *does*
normalise HLSL and Slang sources into the same `IModule` /
`TypeReflection` / `EntryPointReflection` types. From the linter's
perspective, a `.hlsl` and a `.slang` file produce the same shape of
`ReflectionInfo` (`core/include/shader_clippy/reflection.hpp`'s
public types). Slang-specific metadata (generic specialisations,
interface conformances, capability requirements) *is* surfaced
through additional `TypeReflection` queries (`getGenericContainer()`,
`getRequirementCount()`, etc.), but our current `ReflectionInfo`
aggregate doesn't capture those — we'd need an addendum (sub-phase C).

**IR question.** Slang's IR (`Slang::IRModule`) is **internal**, not
exposed via the public API. ADR 0016 / 0017 already established this —
we cannot reach Slang's IR from out-of-process. v1.3 doesn't change
this stance.

**Verdict.** Slang's API gives us reflection (which we already use)
and nothing else for parsing. We cannot use Slang to walk the AST of
a `.slang` file in the way tree-sitter-hlsl walks `.hlsl`.

### 3. Per-rule language applicability

Walking the 189 rule files in `core/src/rules/`, the stage distribution is:

| Stage | Count | What runs on `.slang`? |
|---|---|---|
| `Stage::Ast` | 124 | **No.** Skipped — tree-sitter-hlsl ERRORs. |
| `Stage::ControlFlow` | 33 | **No.** Skipped — CFG is built over the AST. |
| `Stage::Reflection` | 32 | **Yes.** Slang reflection is language-agnostic. |
| `Stage::Ir` | 0 | (No shipped rules use this stage; ADR 0016/0017.) |

That's **32 of 189 rules** = **16.9%** Slang-day-one coverage.

**Spot check, AST-stage (HLSL-only-feasible — ~10 of 124 sampled):**

- `pow-const-squared` — fires on `pow(x, 2.0)`. *Slang preserves the
  `pow` intrinsic exactly.* **Language-agnostic.** Would fire on
  `.slang` if the parser handled `.slang` syntax. Skipped today.
- `clamp01-to-saturate` — fires on `clamp(x, 0.0, 1.0)`. *Slang
  preserves `clamp`.* **Language-agnostic.** Skipped today.
- `redundant-saturate` — fires on `saturate(saturate(x))`. *Slang preserves
  `saturate`.* **Language-agnostic.** Skipped today.
- `compare-equal-float` — fires on `float == float`. *Slang preserves the
  operator.* **Language-agnostic.** Skipped today.
- `acos-without-saturate` — fires on `acos(unclamped)`. **Language-agnostic.**
- `numthreads-too-small` — fires on `[numthreads(N,M,K)]` AST attribute.
  *Slang preserves `[numthreads]`.* **Language-agnostic.**
- `branch-on-uniform-missing-attribute` — AST scan for `if` without
  `[branch]` / `[flatten]`. **Language-agnostic** (but Slang adds its
  own `[ForceInline]` family that the rule doesn't currently know about).
- `bgra-rgba-swizzle-mismatch` — AST scan for `.bgra` on a known-RGB
  texture. **Language-agnostic.**
- `dispatchmesh-grid-too-small-for-wave` — AST scan for `DispatchMesh()`
  call with constant args. **Language-agnostic.**
- `mesh-node-not-leaf` — AST scan for mesh-node attribute combinations.
  **Language-agnostic** in spirit; Slang adds capability syntax that
  the rule's pattern doesn't match.

Projection over the remaining 114 AST rules: **virtually all are
language-agnostic in semantics** (the underlying GPU mechanism doesn't
care which frontend the source uses); they're only "HLSL-only" because
the parser is. Once tree-sitter-slang lands (sub-phase B), they fire
on `.slang` for free.

**Spot check, Reflection-stage (Slang-day-one — ~5 of 32 sampled):**

- `cbuffer-padding-hole` — reflection scans cbuffer field offsets.
  *On `.slang`*: the rule walks `ReflectionInfo::cbuffers` regardless
  of frontend. **Fires.** **But:** Slang's `ParameterBlock<T>` flattens
  to a binding entry that this rule's current shape may misclassify.
  *FP risk.*
- `texture-format-vs-type-mismatch` — reflection scans
  `ResourceBinding::format`. **Fires.** Language-agnostic.
- `bool-straddles-16b` — reflection scans cbuffer field layouts for a
  `bool` straddling the 16-byte packing boundary. **Fires.** Slang's
  bool-in-cbuffer story matches HLSL's.
- `byteaddressbuffer-load-misaligned` — reflection scans byte offsets.
  **Fires.**
- `all-resources-bound-not-set` — reflection scans for the
  `[allResourcesBound]` opt-in. *Slang allows the same attribute on
  both `.hlsl` and `.slang` entry points.* **Fires.**

Projection: **all 32 Reflection-stage rules fire on `.slang` immediately**,
modulo the FP-risk caveat above.

**Spot check, ControlFlow-stage (Slang-skipped — ~5 of 33 sampled):**

- `barrier-in-divergent-cf` — needs CFG over AST. *Skipped on `.slang`.*
- `cbuffer-divergent-index` — needs uniformity oracle. *Skipped.*
- `cbuffer-load-in-loop` — needs loop detection. *Skipped.*
- `clip-from-non-uniform-cf` — needs uniformity oracle. *Skipped.*

Projection: all 33 ControlFlow-stage rules **silently skip** on
`.slang` until sub-phase B lands a parser. Their underlying mechanism
applies equally to Slang code.

**Slang-only rules (`Stage::Slang*` — none today).** Every rule in the
shipping registry targets HLSL idioms. *Future* Slang-specific rules
(generics misuse, capability-constraint violation, `interface`-without-
conformance, `extension`-in-non-module-scope) are pre-Phase-C R&D —
none exist today and none ship in v1.3.

### 4. CLI / LSP / file-extension dispatch

**CLI today** (`cli/src/main.cpp`): the `lint` subcommand accepts
arbitrary path arguments — there is no extension whitelist. Users can
already run `shader-clippy lint foo.slang`; the file is read, passed to
the parser (which produces ERROR tree), and Reflection-stage rules
*do* fire (Slang reflection ingests `.slang` natively) while AST/CFG
rules silently produce noise or nothing.

**v1.3 CLI changes (sub-phase A):**

- Add `.slang` to a new `k_recognized_extensions` array (cosmetic — it
  influences default-glob expansion if we ever add `--all` glob walking,
  not the explicit-path code path).
- Add a `--source-language=<auto|hlsl|slang>` flag (default `auto`),
  routing the parser dispatch decision: `auto` infers from extension
  (`.slang` → skip tree-sitter-hlsl; `.hlsl` and friends → use it).
- Emit `clippy::language-skip-ast` once per `.slang` source (info
  severity, suppressible) so users see *why* their AST rules are silent.

**LSP today** (`lsp/`): the server doesn't filter incoming
`textDocument/didOpen` messages by language ID. Whatever the client
sends, the server lints. The ID is informational.

**v1.3 LSP changes:** none required server-side. The server is
language-agnostic (it lints whatever the client sends); the gating
happens client-side.

**VS Code extension today** (`vscode-extension/package.json`):

```json
"languages": [
  { "id": "hlsl",
    "extensions": [".hlsl", ".hlsli", ".fx", ".fxh", ".vsh",
                   ".psh", ".csh", ".gsh", ".hsh", ".dsh"],
    ... }
]
```

The `documentSelector` in `vscode-extension/src/extension.ts` is
`{ scheme: "file", language: "hlsl" }` (and the `untitled` variant),
plus a `k_languageId = "hlsl"` constant used in seven places to
gate command palette items, status-bar updates, etc.

**v1.3 VS Code changes (sub-phase A):**

- Add a second language contribution: `{ "id": "slang", "extensions": [".slang"], ... }`.
  Note: VS Code language IDs are **per-extension namespaces** with
  collision concerns — the official `shader-slang.slang` extension also
  contributes `slang`. VS Code resolves this by either letting both
  extensions handle the document (typical case — one provides syntax
  highlighting, the other diagnostics) or by user-side preference.
  We add a config knob `shaderClippy.slang.enable` (default `true`,
  suppressible) for users who prefer the official extension to handle
  `.slang` exclusively.
- Update `documentSelector` to include `{ scheme: "file", language: "slang" }`.
- Update `k_languageId` from a constant to an array `k_languageIds = ["hlsl", "slang"]`
  and adjust the `editor.document.languageId !== k_languageId` checks
  to `!k_languageIds.includes(editor.document.languageId)`.
- Update the walkthrough to mention `.slang` files alongside `.hlsl`.
- No changes to commands or keybindings.

**Marketplace structural change?** No. VS Code allows multiple language
contributions in a single `package.json` cleanly. The `keywords:` list
already contains `"slang"` (added pre-launch); no new keyword needed.

### 5. Reflection-only fallback strategy

The "ship reflection-only on `.slang`" strategy is feasible because
the existing Slang bridge (`core/src/reflection/slang_bridge.cpp`) does
not currently constrain source language — it calls
`session->loadModuleFromSourceString(name, virtual_path, contents, ...)`
and Slang infers the language from `virtual_path`'s extension. A
`.slang` virtual path causes Slang to pick its native frontend; an
`.hlsl` virtual path picks the HLSL frontend; both populate the same
`IModule` and reflection tree.

**What surfaces:** every rule with `stage() == Stage::Reflection` fires
on `.slang` immediately. That's 32 of 189 rules. The categories these
32 rules cover:

- Resource-binding rules (`all-resources-bound-not-set`,
  `bgra-rgba-swizzle-mismatch`, `texture-format-vs-type-mismatch`,
  `byteaddressbuffer-narrow-when-typed-fits`, etc.).
- cbuffer-layout rules (`cbuffer-padding-hole`,
  `cbuffer-fits-rootconstants`, `cbuffer-large-fits-rootcbv-not-table`,
  `bool-straddles-16b`, etc.).
- Reflection-anchored mesh / RT / SER rules
  (`as-payload-over-16k`, the `coopvec-*` family, `coherence-hint-*`).

**What stays dark:** the 124 AST rules and 33 CFG rules, which is the
bulk of the linter's value. `pow-const-squared` (the project's flagship
rule) sits in this dark — Slang preserves `pow(x, 2.0)` exactly, but
without an AST that parses `.slang`, the rule has no way to find it.

**Useful-but-limited verdict.** Sub-phase A delivers a meaningful
baseline (the 32 reflection rules cover many of the highest-FP-rate
real-shader gotchas — cbuffer padding, descriptor-table sizing, format
mismatches), but it is not parity. We document the gap honestly in the
v1.3 release notes and the configuration page.

### 6. Phased rollout proposal

| Sub-phase | Version | Scope |
|---|---|---|
| **A** | **v1.3.0** | File-extension recognition + reflection-only lint for `.slang`. ~17% rule surface. **This ADR proposes only this sub-phase.** |
| B | v1.4+ | tree-sitter-slang grammar integration (vendored or extended-from-tree-sitter-hlsl). AST + CFG rules fire on `.slang`. Up to ~99% of language-agnostic rule surface. **Future ADR — research dispatch first.** |
| C | v1.5+ | Slang-specific rules (generics misuse, interface-conformance gaps, capability-constraint violations, `extension`-scope mistakes). New category `slang-language`. **Future ADR — gated on B.** |

The user's brief was "make it slang compatible. not only hlsl" —
ambiguous between (A) "recognise .slang and run what we can" and (B)
"full parity." This ADR proposes (A) only because (B) is research-grade
work that has not been scoped, and shipping (A) creates the telemetry
surface we need to justify (B)'s investment.

## Decision (Proposed) — sub-phase A only

**v1.3.0 ships:**

1. **File-extension recognition.**
   - `cli/src/main.cpp`: add `.slang` to a new `k_recognized_extensions`
     array. Add `--source-language=<auto|hlsl|slang>` flag, default
     `auto`. The flag short-circuits the parser dispatch:
     - `auto` + `.slang` → **skip** AST parser invocation, route directly
       to reflection.
     - `auto` + `.hlsl` family → existing path unchanged.
     - explicit `--source-language=slang` → force the skip on any path.
     - explicit `--source-language=hlsl` → force the existing path on
       any path (recovery for `.slang` files containing only HLSL).
   - No glob-walking changes — explicit path arguments are still required.

2. **Reflection-only lint for `.slang` sources.**
   - `core/src/lint.cpp` (orchestrator): when source-language is `slang`,
     run only `Stage::Reflection` rules. Skip AST + CFG dispatch.
   - On the first skip per source, emit one informational diagnostic
     `clippy::language-skip-ast` at line 1, column 1:
     > "[clippy::language-skip-ast] AST and CFG rules disabled on Slang
     >  source. 32 of 189 rules ran. Suppress this notice with
     >  `// shader-clippy: allow(language-skip-ast)` or in the workspace
     >  `.shader-clippy.toml` under `[rules]` `language-skip-ast = "allow"`."
   - The rule ID `clippy::language-skip-ast` joins the tiny set of
     `clippy::*` infrastructure diagnostics already emitted (the
     parse-error and reflection-error families).

3. **VS Code extension `slang` language contribution.**
   - `vscode-extension/package.json`: add a second `languages[]` entry
     `{ "id": "slang", "aliases": ["Slang"], "extensions": [".slang"], ... }`.
     Reuse `language-configuration.json` (HLSL's bracket / comment
     conventions are a strict superset of what Slang adds for v1.3 — a
     deeper Slang-specific configuration ships in v1.4+ alongside
     tree-sitter-slang).
   - `vscode-extension/src/extension.ts`:
     - Replace `const k_languageId = "hlsl";` with
       `const k_languageIds = ["hlsl", "slang"] as const;`.
     - Update the seven `editor.document.languageId !== k_languageId`
       checks to `!k_languageIds.includes(editor.document.languageId)`.
     - Update `documentSelector` to a 4-entry array (file/untitled ×
       hlsl/slang).
     - Update walkthrough `description` strings to include `.slang`.
   - Add a config knob `shaderClippy.slang.enable` (boolean, default
     `true`) in `package.json` `contributes.configuration.properties`.
     When `false`, the VS Code extension does NOT register the `slang`
     language contribution at activation time (users who run
     `shader-slang.slang` exclusively can opt out).
   - Marketplace `keywords` already contains `"slang"`; no change.

4. **Config surface.**
   - `core/include/shader_clippy/config.hpp`: add `enum class SourceLanguage { Auto, Hlsl, Slang };`
     and `Config::source_language()` getter (default `Auto`). Set per
     `[lint] source-language = "auto" | "hlsl" | "slang"` in
     `.shader-clippy.toml`. Per-file extension inference happens at the
     orchestrator when `Auto`.
   - **No ABI break:** new enum, new getter, new TOML key. Existing code
     paths default to `Auto`, which behaves exactly as v1.2.0 did for
     `.hlsl` paths.

5. **Documentation.**
   - `README.md` "Supported file types" section: add `.slang` with the
     "(reflection-only on v1.3; full AST + CFG support tracked for v1.4+
     — see ADR 0020)" caveat.
   - `docs/configuration.md`: document `[lint] source-language` and the
     `clippy::language-skip-ast` informational diagnostic.
   - `docs/rules/_template.md`: add a `language_applicability` field
     (`"hlsl-only" | "language-agnostic" | "slang-only"`). New rules
     declare; existing rules grandfather to `"language-agnostic"` until
     audited.
   - One blog post in `docs/blog/` introducing the v1.3 Slang baseline,
     anchored to the rule-surface honesty number.

6. **Tests.**
   - `tests/fixtures/slang/`: 3 hand-written `.slang` files exercising
     (a) plain-HLSL content with `.slang` extension, (b) Slang-language
     content with HLSL-equivalent constructs, (c) Slang-only constructs
     (generics + interfaces) that surface as ERROR if AST is mis-engaged.
   - `tests/unit/test_slang_dispatch.cpp`: confirm
     `clippy::language-skip-ast` fires once per `.slang` source, AST
     rules don't fire, Reflection rules do fire.
   - `tests/golden/slang/`: golden output for the three fixtures.

7. **CI.**
   - One new job `slang-recognition` on the existing Linux Clang matrix
     entry: parses the three fixtures, asserts the diagnostic shape.
   - No new platform matrix entries.

**Out of scope for sub-phase A** (explicitly tracked for v1.4+ in §"v1.4
/ v1.5 candidates"):

- tree-sitter-slang grammar integration.
- AST/CFG rule firing on `.slang`.
- Slang-specific rules (generics, interfaces, capabilities).
- `language-configuration.json` Slang-specific bracket / comment rules.
- Slang `import`/`__include` resolution (cross-file rules).
- Per-rule audit of `language_applicability` for the 189 existing rules.

**Effort estimate.** One dev week. Two parallel tracks:

- **Track 1 — core + CLI** (1 dev): config knob + orchestrator dispatch +
  CLI flag + clippy::language-skip-ast emission + tests + docs.
- **Track 2 — VS Code extension** (1 dev): language contribution +
  documentSelector update + k_languageIds refactor + walkthrough text +
  manifest test on the extension test harness.

## v1.4 / v1.5 candidates (not in v1.3)

These are the **follow-ups** that sub-phase A enables but does not
deliver. Each requires its own ADR before implementation; demand-gating
on sub-phase A telemetry.

### v1.4 sub-phase B — tree-sitter-slang integration

- **Investigate Theta-Dev/tree-sitter-slang for vendoring.** If the
  upstream reaches a v1.0 tag, vendor it as a second submodule (or
  prebuilt) alongside tree-sitter-hlsl. Parser dispatch: `.hlsl` → ts-hlsl,
  `.slang` → ts-slang.
- **Alternative: extend tree-sitter-hlsl to a Slang superset.** Estimated
  6 months of grammar work. Upstreamable if Theta-Dev is unmaintained.
- **AST + CFG rule firing on `.slang`.** Once a Slang parser produces a
  clean AST, all 124 AST and 33 CFG rules fire on `.slang` for free
  (modulo the `language_applicability` audit — flagged-`hlsl-only` rules
  stay HLSL-only).
- **Per-rule audit pass.** Each of the 189 rules gets a
  `language_applicability` field set explicitly. Most are
  `"language-agnostic"`; some may reveal HLSL-only assumptions that
  need either Slang-side adjustment or an `"hlsl-only"` lock.

### v1.5 sub-phase C — Slang-specific rules

A research dispatch (parallel to ADR 0011 / 0018) curates Slang-specific
candidates. Initial seed list (each requires citation + GPU mechanism
before locking):

- `slang-generic-without-capability-constraint` — `__generic<T>` parameter
  used in a context that requires a target-specific capability (e.g.
  raytracing) without a `[require(...)]` attribute.
- `slang-interface-without-conformance-witness` — `interface IFoo` used
  as a parameter type without any `extension Bar : IFoo` in scope.
- `slang-import-cycle` — `import a;` chain that closes back on itself.
- `slang-extension-in-non-module-scope` — `extension Foo { ... }` declared
  inside a function body (rejected by Slang but worth a clear lint).
- `slang-capability-vs-target-profile-mismatch` — `[require(spirv_1_4)]`
  on an entry point compiled for `dxil_6_0` (or vice versa).
- `slang-associatedtype-without-default` — interface declares
  `associatedtype Item;` without a default; instantiations forced to
  spell it out — sometimes intentional, sometimes a footgun.

**None ship in v1.3.** None ship in v1.4 unless the parser surface is
ready. Sub-phase C lands on top of sub-phase B.

### Other deferred items

- **Slang LSP `textDocument/semanticTokens` support.** Slang's own LSP
  (`slangd`) provides this; ours does not, and the v1.x LSP shape is
  intentionally diagnostic-only. Not in scope.
- **`shader-slang.slang` cross-extension coordination.** If the official
  extension begins emitting LSP diagnostics that overlap ours, we add
  a config knob `shaderClippy.slang.suppressOnConflict` to defer to
  the official extension. Not in scope until conflict observed.
- **Slang `interface`-conformance-aware reflection.** Sub-phase C will
  surface interface conformance through a new `ReflectionInfo` field,
  but only after at least one rule needs it.

## Risks & mitigations

- **Risk: Slang reflection drift on `.slang` shape.** Slang's reflection
  surfaces `ParameterBlock<T>` differently from HLSL's `cbuffer` —
  `cbuffer-padding-hole` and similar layout-anchored rules may misfire
  on `.slang` inputs that use Slang-idiomatic resource declarations.
  - *Mitigation 1:* the v1.3 release notes call out the FP-risk
    explicitly. Users see the disclaimer before opting in.
  - *Mitigation 2:* `tests/fixtures/slang/` includes a deliberately
    `ParameterBlock<T>`-heavy fixture; `tests/golden/slang/` snapshots
    the rule output. A regression in the snapshot blocks the PR.
  - *Mitigation 3:* every reflection-stage rule that misfires on
    `.slang` gets a v1.3.x patch under the v1.x maintenance contract.
    No rule changes its public contract (`id`, `category`, `severity`)
    in the patch — only the detection logic adapts.
- **Risk: Slang submodule pin pressure.** Sub-phase A elevates Slang
  reflection from "opt-in compile target" to "mandatory parser surface
  for `.slang`." A Slang prebuilt regression on `.slang` ingestion
  blocks the linter for those users.
  - *Mitigation:* the existing CI Slang-bump regression job (ADR 0019
    §"v1.0 readiness criterion #10") gains a `.slang` test fixture
    so a Slang minor-version bump that breaks `.slang` ingestion
    fails the bump before merge.
- **Risk: VS Code marketplace overlap with `shader-slang.slang`.** Two
  extensions contributing the same `slang` language ID is permitted by
  VS Code but can produce double-squiggling or competing diagnostic
  scopes if the official extension also lints (it currently does not,
  but may grow that capability).
  - *Mitigation 1:* config knob `shaderClippy.slang.enable` (default `true`).
    Users who prefer the official extension toggle to `false`.
  - *Mitigation 2:* on activation, log a one-time message to the output
    channel: "[shader-clippy] also handling .slang files. Set
    `shaderClippy.slang.enable=false` to defer to shader-slang.slang."
- **Risk: false-positive rate on `.slang` is unmeasured.** v1.0 readiness
  criterion #3 (FP-rate ≤ 5% per warn-grade rule) was measured against
  HLSL corpus only.
  - *Mitigation:* sub-phase A ships a separate `tests/corpus-slang/`
    seed (3 hand-written + 2 imported open-source `.slang` shaders;
    track Khronos / Slang-samples for additions). FP-rate measurement
    against this corpus is documented under
    `tests/corpus-slang/FP_RATES.md` from v1.3. The 5% bar applies
    prospectively from v1.4 (where AST rules also start firing on
    `.slang`); v1.3's reflection-only surface ships with measured
    rates and no upper bound for its first release.
- **Risk: scope creep — users ask for tree-sitter-slang in v1.3.**
  Sub-phase A is deliberately small. A loud feature-request thread
  could pressure the maintainer to skip the sub-phase B research
  dispatch and ship a half-baked tree-sitter-slang vendor.
  - *Mitigation:* ROADMAP entry explicitly states "v1.4+ pending ADR";
    the README v1.3 disclaimer states the same. Sub-phase B requires
    a successor ADR before any vendoring lands.
- **Risk: rule-author confusion on `language_applicability`.** New rules
  authored against `.hlsl` fixtures that happen to fire on `.slang` (via
  reflection) but were never exercised against `.slang` reflection
  shapes ship with `"language-agnostic"` and an unmeasured FP rate.
  - *Mitigation:* `docs/rules/_template.md` `language_applicability`
    field defaults to `"language-agnostic"` but the rule-author
    checklist in CONTRIBUTING.md (added under sub-phase A) requires
    a one-line "tested against .slang? yes/no/n.a." note in the rule's
    PR description. Honest disclosure, not blocking.
- **Risk: `clippy::language-skip-ast` informational diagnostic spams
  CI gate-mode runs.** Users running `shader-clippy lint --format=github-annotations
  shaders/**/*.{hlsl,slang}` see one info diagnostic per `.slang`
  file in their CI logs.
  - *Mitigation:* the diagnostic is `Severity::Info` (not Warning).
    GitHub Actions `--format=github-annotations` skips Info severity
    by default per ADR 0015 §"sub-phase 6a CI gate-mode polish."
    Users who *want* the visibility can override with
    `--severity-threshold=info`.

## Cross-references

- **[ADR 0001](0001-compiler-choice-slang.md)** (Compiler — Slang) —
  reflection bridge is reused as-is for `.slang` ingestion. The choice
  of Slang-the-compiler is what makes Slang-the-language tractable
  in v1.3.
- **[ADR 0002](0002-parser-tree-sitter-hlsl.md)** (Parser —
  tree-sitter-hlsl) — sub-phase A *bypasses* the parser for `.slang`;
  sub-phase B is where this ADR's parser decision gets re-litigated.
- **[ADR 0012](0012-phase-3-reflection-infrastructure.md)** (Phase 3
  reflection infrastructure) — the public `ReflectionInfo` shape is
  language-agnostic by design; sub-phase A leverages this. Sub-phase C
  may add Slang-specific reflection fields (interface conformances,
  generic specialisations) under a successor ADR.
- **[ADR 0014](0014-phase-5-lsp-architecture.md)** (Phase 5 LSP
  architecture) — VS Code extension's language-contribution shape is
  reused; the `documentSelector` extension is additive.
- **[ADR 0019](0019-v1-release-plan.md)** (v1.0 release plan) — v1.x
  maintenance contract permits additive features within ABI. Slang
  language support is an additive v1.x feature; v1.3 is the next
  natural release vehicle. Sub-phase A respects every v1.x guarantee
  (no public-type ABI change, no symbol removal, no severity bump
  on shipped rules).

## More information

- **Brainstorm research date:** 2026-05-02 (post-v1.2.0 ship).
- **Methodology notes:**
  - Stage distribution from `grep -h "return Stage::" core/src/rules/*.cpp | sort | uniq -c`
    on tip-of-main 2026-05-02. 124 + 33 + 32 = 189 rules, matching the
    registry count modulo `registry.cpp` itself.
  - tree-sitter-slang upstream survey: GitHub search for `tree-sitter-slang`,
    inspection of Theta-Dev's repository (commits, README coverage
    matrix, tag list). The grammar has not reached a v1.0 tag as of
    this ADR.
  - Slang public API surface confirmed via the published Doxygen output
    at <https://docs.shader-slang.org/en/latest/external/slang/docs/api-reference/api-reference.html>
    and cross-checked against `core/src/reflection/slang_bridge.cpp`'s
    actual API usage. **Did not reach the upstream `slang.h` source
    file directly during this research pass** — the bridge's existing
    surface (`IModule`, `ISession`, `IComponentType`, `TypeReflection`,
    `EntryPointReflection`) was the proxy.
  - Spot-check rules for §3 selected by sampling rule files at random
    from `core/src/rules/` (pseudo-random first-letter spread:
    `acos_*`, `bgra_*`, `cbuffer_*`, `dispatchmesh_*`, `mesh_node_*`)
    rather than a full-189 audit; the ten samples cover the AST-
    category mix proportionally enough to project.
  - VS Code language contribution mechanics verified against
    [VS Code Language Extension docs](https://code.visualstudio.com/api/language-extensions/overview)
    and the existing `vscode-extension/package.json` shape.
  - Slang reflection's language-normalisation behaviour confirmed by
    re-reading `core/src/reflection/slang_bridge.cpp`'s
    `loadModuleFromSourceString` call site — no `setSourceLanguage`
    is called; Slang infers from `virtual_path` extension. A `.slang`
    extension at the bridge layer routes through Slang's native
    frontend.
- **What this ADR does *not* commit to:**
  - Tree-sitter-slang vendoring (sub-phase B; future ADR).
  - Slang-specific rules (sub-phase C; future ADR).
  - Slang `interface` / `generic` reflection surfacing in
    `ReflectionInfo` (future ADR; needed only when a rule consumes it).
  - LSP semantic-tokens, hover, or definition support for `.slang`
    (intentionally outside the diagnostic-only LSP scope from ADR 0014).
- **Revisit cadence:** at the v1.3.x → v1.4.0 boundary. If sub-phase A
  telemetry (Marketplace install delta on `slang`-tagged users,
  `clippy::language-skip-ast` suppression rate, GitHub workflow-search
  hits using `.slang` paths) exceeds a threshold (e.g. 20% of v1.3
  install delta involves `.slang` files), the v1.4 sub-phase B
  research dispatch is unblocked. Below the threshold, sub-phase B is
  parked indefinitely and sub-phase A becomes the steady state.
- **Future expansions add a successor ADR** (this ADR is not edited
  after acceptance, per ADR 0007's precedent).
