---
status: Proposed
date: 2026-05-01
deciders: NelCit
tags: [phase-3, reflection, slang, rule-engine, infrastructure, planning]
---

# Phase 3 reflection infrastructure — Slang reflection plumbed into RuleContext

## Context and Problem Statement

Today the lint pipeline (`core/src/lint.cpp`) is AST-only. It parses each
source through tree-sitter-hlsl, walks the named-node tree, and dispatches
each `Rule::on_node` / `Rule::on_tree` hook against a neutral wrapper
(`AstCursor` / `AstTree`). No rule has access to resource-binding
information, cbuffer layout, sampler descriptor state, entry-point shape,
or texture format — because none of those are derivable from a CST alone.

Phase 3 is the first phase that needs that information. Across the locked
rule decisions:

- ADR 0007 §Phase 3 — 15 reflection-aware rules (resource binding, cbuffer
  layout, sampler descriptor, entry-point stage).
- ADR 0010 §Phase 3 — 23 SM 6.7/6.8/6.9 rules (SER, Cooperative Vectors,
  Long Vectors, OMM, Mesh Nodes — all of which need at least
  resource-kind / target-profile / entry-point information).
- ADR 0011 §Phase 3 — 17 newly LOCKED rules (buffers, samplers,
  root-signature ergonomics, compute pipeline shape, wave-quad extras
  that depend on `[WaveSize]` / `[numthreads]` reflection).

That is **~55 rules blocked on a single piece of infrastructure that
has not yet been designed**: a way to expose Slang's reflection API to
the lint engine without violating the four constraints already locked
elsewhere in the codebase:

1. **`<slang.h>` MUST NOT appear in `core/include/hlsl_clippy/`.** CI
   grep enforces this; same constraint applies to `<tree_sitter/api.h>`.
   See ADR 0001 + CLAUDE.md "What NOT to do".
2. **Slang `IGlobalSession` is not thread-safe.** Maintain one global
   session and a per-worker `ISession` pool. See CLAUDE.md "Known
   issues to plan around".
3. **No exceptions across the `core` API boundary.** Reflection
   failures surface as `std::expected<T, Diagnostic>`. See ADR 0004 +
   CLAUDE.md "Code standards — Ban list".
4. **AST-only lint runs (Phase 0/1/2 rule selection) must stay fast.**
   Linting a 50-rule shader-tree against the math-pack should not
   incur Slang compile cost.

This ADR proposes the smallest viable architecture that satisfies all
four constraints, and lays out the sub-phase order in which the
infrastructure lands so the three Phase 3 rule packs (ADR 0011 §Phase 3
plan) can dispatch in parallel against it. **No code is written by this
ADR.** It is a plan, in the same shape as ADR 0008 (Phase 1) and ADR
0009 (Phase 2).

## Decision Drivers

- **Don't leak `<slang.h>` through public headers.** Reflection types
  exposed to rule authors must be opaque value types defined in
  `core/include/hlsl_clippy/reflection.hpp`; the only translation
  unit that includes `<slang.h>` is the bridge implementation under
  `core/src/reflection/slang_bridge.cpp`. CI grep already enforces this
  constraint and must continue to pass.
- **Slang `IGlobalSession` is not thread-safe.** Architecture pools
  per-worker `ISession` instances behind a single lazily-initialised
  `IGlobalSession`. The pool size is a configurable knob (default 4);
  rule dispatch within one source is single-threaded, so the pool only
  matters when (future Phase 5) the LSP server runs multiple lint passes
  in parallel.
- **`std::expected<ReflectionInfo, Diagnostic>` at the API boundary.**
  Slang compile errors surface as a `Diagnostic` with `severity =
  Error` and code `clippy::reflection`, anchored to a best-effort
  `(SourceId, ByteSpan)`. No exceptions cross from the bridge into
  the rule engine.
- **Lazy invocation.** A lint run that selects only AST rules
  (Phase 0/1/2 rule pack) MUST NOT trigger Slang compilation. The
  pipeline inspects every enabled rule's `stage()` and only constructs
  the reflection engine if at least one rule has
  `stage() == Stage::Reflection`.
- **Reflection fan-out is real.** One HLSL file may carry multiple
  entry points (vertex + pixel in a forward shader; compute + amplification
  in a mesh-pipeline shader) and may target multiple back-ends (DXIL
  + SPIR-V for cross-platform engines). The architecture surfaces this
  as a vector of `EntryPointInfo` per `ReflectionInfo`, and a
  `target_profile` field per `ReflectionInfo`. Per-source
  multi-target reflection is deferred (see "Open question" below).
- **Stable spans.** Reflection diagnostics still use
  `(SourceId, ByteSpan)` so they integrate with the existing
  `Diagnostic` shape unchanged. When reflection identifies a problem
  but the AST doesn't carry a precise anchor (e.g. a padding hole
  inside a cbuffer field list), the rule emits the cbuffer
  declaration's byte-span — never a synthetic span.
- **Cacheable.** Reflecting the same `(SourceId, target_profile)`
  twice in a single lint run reuses the cached result. Cache lifetime
  is the lint-run, not the process, because in batch / CI mode each
  run gets a fresh `SourceManager`.

## Considered Options

### Option A — Eager full-reflection

Lint pipeline reflects every source up front, populates a
`ReflectionInfo`, passes it to every rule regardless of stage. Always
pays Slang compile cost, even for runs that select only AST rules.

- Good: single code path; rules don't have to declare their stage
  precisely.
- Bad: violates "AST-only runs stay fast". A user running just
  `pow-const-squared` against a 5,000-line shader pays a multi-second
  Slang compile per source. Reputation hit.
- Bad: macOS deferred to Phase 5 (per CLAUDE.md), but every Linux/Windows
  AST run still pays the cost.

**Rejected.**

### Option B — Per-rule on-demand reflection (synchronous)

Each Phase 3 rule asks `RuleContext::reflect()` and Slang runs
synchronously inside the rule body. Result is cached per
`RuleContext` so repeated calls within one rule's invocation reuse
work, but separate rules invoke separate compiles.

- Good: lazy by construction; AST-only runs pay nothing.
- Bad: 17 reflection rules × N sources = 17N Slang compiles unless every
  rule remembers to share the cache. Easy to regress.
- Bad: serializes a (future) parallel-rule pipeline because each rule's
  compile invocation blocks.
- Bad: makes `RuleContext` carry mutable state that crosses the
  rule-engine / Slang-bridge boundary.

**Rejected.**

### Option C — Two-stage pipeline with cached reflection (chosen)

The `lint()` orchestrator runs in two stages:

1. **AST stage.** Same as today. Every rule with `stage() == Stage::Ast`
   gets `on_tree` + `on_node`.
2. **Reflection stage.** Runs ONLY if at least one enabled rule has
   `stage() == Stage::Reflection`. The orchestrator invokes the
   `ReflectionEngine` once per source, caches the resulting
   `ReflectionInfo` keyed by `(SourceId, target_profile)`, and
   dispatches each reflection-stage rule's `on_reflection` hook with
   that info.

If the reflection engine returns
`std::unexpected(Diagnostic{...})`, the diagnostic is appended to the
lint output (so the user sees the Slang error) and reflection rules are
skipped for that source — they don't crash, they just don't fire.

- Good: AST-only runs pay zero Slang cost (engine never constructs).
- Good: 17 reflection rules share one compile per source per profile.
- Good: the shape of the additive change is small — one new public
  header, one new `Stage` value, one new virtual on `Rule`, one new
  options struct on `lint()`.
- Good: pipeline parallelism (Phase 5 LSP) gets a clean seam — the
  engine can dispatch reflection-stage rules concurrently against an
  immutable `ReflectionInfo`.

**Chosen.**

## Decision Outcome

The proposed architecture, broken into the six concrete API additions
that constitute it.

### 1. New public header `core/include/hlsl_clippy/reflection.hpp` (opaque types only)

No `<slang.h>` include. Pure value types. Sketch:

```cpp
namespace hlsl_clippy {

enum class ResourceKind : std::uint8_t {
    Unknown,
    Texture1D, Texture2D, Texture3D, TextureCube,
    Texture1DArray, Texture2DArray, TextureCubeArray,
    RWTexture1D, RWTexture2D, RWTexture3D,
    RWTexture1DArray, RWTexture2DArray,
    Buffer, RWBuffer,
    ByteAddressBuffer, RWByteAddressBuffer,
    StructuredBuffer, RWStructuredBuffer,
    AppendStructuredBuffer, ConsumeStructuredBuffer,
    SamplerState, SamplerComparisonState,
    ConstantBuffer,
    AccelerationStructure,
    FeedbackTexture2D, FeedbackTexture2DArray,
};

struct ResourceBinding {
    std::string  name;            // shader-side identifier
    ResourceKind kind  = ResourceKind::Unknown;
    std::uint32_t register_slot   = 0;
    std::uint32_t register_space  = 0;
    std::optional<std::uint32_t> array_size;  // unbounded = nullopt
    Span declaration_span{};      // best-effort AST anchor
};

struct CBufferField {
    std::string  name;
    std::uint32_t byte_offset = 0;
    std::uint32_t byte_size   = 0;
    std::string  type_name;       // for diagnostic rendering
};

struct CBufferLayout {
    std::string  name;
    std::uint32_t total_bytes = 0;
    std::vector<CBufferField> fields;
    Span declaration_span{};

    // Helpers for padding-hole / alignment rules:
    [[nodiscard]] std::uint32_t padding_bytes() const noexcept;
    [[nodiscard]] bool is_16byte_aligned() const noexcept;
};

struct EntryPointInfo {
    std::string  name;
    std::string  stage;           // "vertex" / "pixel" / "compute" / "mesh" / ...
    std::optional<std::array<std::uint32_t, 3>> numthreads;
    std::optional<std::uint32_t> wave_size_min;
    std::optional<std::uint32_t> wave_size_max;
    Span declaration_span{};
};

struct ReflectionInfo {
    std::vector<ResourceBinding> bindings;
    std::vector<CBufferLayout>   cbuffers;
    std::vector<EntryPointInfo>  entry_points;
    std::string                  target_profile;  // e.g. "sm_6_6"

    [[nodiscard]] const ResourceBinding* find_binding_by_name(
        std::string_view name) const noexcept;
    [[nodiscard]] const CBufferLayout*   find_cbuffer_by_name(
        std::string_view name) const noexcept;
    [[nodiscard]] const EntryPointInfo*  find_entry_point_by_name(
        std::string_view name) const noexcept;
};

}  // namespace hlsl_clippy
```

Every type is a copyable / movable value. No `unique_ptr`s to opaque
Slang handles cross the public boundary; the bridge is responsible for
walking Slang's reflection tree once and materialising these flat
structs.

### 2. Extend `Stage` enum in `core/include/hlsl_clippy/rule.hpp`

```cpp
enum class Stage : std::uint8_t {
    Ast,         // AST-only (default; Phase 0/1/2)
    Reflection,  // needs ReflectionInfo (Phase 3)
    // Future: ControlFlow (Phase 4), Ir (Phase 7).
};
```

The default `Rule::stage()` continues to return `Stage::Ast`, so all
existing Phase 0/1/2 rules keep their current behaviour with zero
source change.

### 3. New rule entrypoint `Rule::on_reflection`

Add a third virtual alongside `on_node` and `on_tree`:

```cpp
class Rule {
    // ... existing methods unchanged ...

    /// Reflection-stage hook. Called once per source with the
    /// already-cached `ReflectionInfo`. Default implementation does
    /// nothing.
    virtual void on_reflection(const AstTree& tree,
                               const ReflectionInfo& reflection,
                               RuleContext& ctx);
};
```

Rules with `stage() == Stage::Reflection` override this. Reflection
rules retain access to the `AstTree` because most of them want both
sides — reflection tells them *what* the resource is, the AST tells
them *where* in source it was used. The orchestrator never calls
`on_reflection` for `Stage::Ast` rules and never calls `on_tree` /
`on_node` for `Stage::Reflection` rules unless the rule explicitly
opts in by overriding both (legitimate: a rule that does AST-side
filtering and reflection-side validation in one body).

### 4. Internal `ReflectionEngine` (lives in `core/src/reflection/`)

New private module:

```
core/src/reflection/
    engine.hpp            // public-to-core API
    engine.cpp            // session pool + cache
    slang_bridge.hpp      // narrow bridge surface
    slang_bridge.cpp      // ONLY TU that includes <slang.h>
```

Responsibilities of `ReflectionEngine`:

- Owns the global Slang `IGlobalSession`. Lazily initialised on first
  use; outlives every `lint()` invocation (process-lifetime singleton
  via `Meyers` pattern; `IGlobalSession` is documented as expensive to
  construct). Thread-safety is enforced by holding the global session
  read-only after init and never mutating it.
- Per-`ReflectionEngine`-instance pool of `ISession` workers. Default
  pool size 4; configurable via `LintOptions::reflection_pool_size`.
  One `ISession` is acquired, used to compile + reflect, then returned.
- Per-lint-run cache keyed by `(SourceId, target_profile)`. Cache
  uses `std::flat_map` per CLAUDE.md "C++23 idioms to use".
- Returns `std::expected<ReflectionInfo, Diagnostic>`. Slang compile
  errors get translated into a `Diagnostic` with `code =
  "clippy::reflection"`, `severity = Severity::Error`, primary span
  set to the best-effort `(SourceId, byte_span)` parsed out of Slang's
  diagnostic message — falling back to `ByteSpan{0, 0}` if Slang
  reports no source location.

The `slang_bridge.cpp` translation unit is the **only** place
`<slang.h>` is included anywhere under `core/`. CI grep that already
forbids `<slang.h>` in `core/include/hlsl_clippy/` keeps passing
unchanged; a new CI grep clause may want to assert `<slang.h>` is
ALSO confined to `core/src/reflection/` (i.e., `core/src/rules/` stays
free of it). That CI tweak is a follow-up under sub-phase 3a.

### 5. Extended `lint()` signature with `LintOptions`

Additive change. The two existing `lint()` overloads keep their
current signatures and behaviour. A third overload is added:

```cpp
struct LintOptions {
    /// Override the default target profile. When empty, the engine
    /// picks per detected entry-point stage (`sm_6_6` baseline:
    /// `vs_6_6` / `ps_6_6` / `cs_6_6` / etc).
    std::optional<std::string> target_profile;

    /// When false, the reflection stage is skipped entirely even if
    /// reflection-stage rules are enabled. Useful for fast iteration /
    /// AST-only smoke runs / tests that want to isolate AST behaviour.
    bool enable_reflection = true;

    /// Pool size for `ISession` workers. Default 4; LSP / batch CI
    /// may want more.
    std::uint32_t reflection_pool_size = 4;
};

[[nodiscard]] std::vector<Diagnostic> lint(
    const SourceManager& sources,
    SourceId source,
    std::span<const std::unique_ptr<Rule>> rules,
    const LintOptions& options);
```

A `Config`-aware overload taking both `Config` and `LintOptions` is
added in the same shape as the existing `Config` overload.

The pipeline inspects every enabled rule's `stage()` once at the start
of `lint()`. If no rule has `stage() == Stage::Reflection`, the
reflection engine is never constructed and `LintOptions::target_profile`
is silently ignored. This is what keeps AST-only runs fast.

### 6. Shared utilities for Phase 3 rules

Per ADR 0011 §Phase 3 plan + ADR 0009's pattern, three shared-utility
headers in `core/src/rules/util/`:

- `reflect_resource.hpp/cpp` — `find_resource(reflection, name)`,
  `is_writable(kind)`, `is_texture(kind)`, `is_uav(kind)`,
  `dimension(kind)`, `array_size_or(default)`.
- `reflect_sampler.hpp/cpp` — sampler descriptor field accessors when
  Slang surfaces them via reflection (filter, address mode,
  MaxAnisotropy, MaxLOD, ComparisonFunc); fallback path that reads the
  AST when Slang's reflection doesn't carry the descriptor (static
  samplers declared in HLSL).
- `reflect_stage.hpp/cpp` — `find_entry_point(reflection, name)`,
  `is_compute_stage(stage_str)`, `target_sm_at_least(profile, "sm_6_6")`,
  `numthreads_total(entry_point)`, `wave_size_clamped(entry_point)`.

These are private headers; rules `#include "rules/util/..."` and
never see Slang types directly.

## Implementation sub-phases

Mirrors ADR 0009 / ADR 0011's "shared-utilities PR + parallel category
packs" pattern. **Do not parallelise sub-phases 3a and 3b** — they
share design surface and serialising them avoids merge-time drift.
Sub-phase 3c is the parallel-pack dispatch.

### Sub-phase 3a — infrastructure PR (sequential, must land first)

Single PR, single agent. Lands:

- `core/include/hlsl_clippy/reflection.hpp` (opaque types per §1).
- New private module `core/src/reflection/{engine,slang_bridge}.{hpp,cpp}`.
  `slang_bridge.cpp` is the only TU that includes `<slang.h>`.
- Extend `Stage` enum (§2) and `Rule::on_reflection` virtual (§3).
- Extend `lint()` with `LintOptions` overload (§5). Existing overloads
  preserved.
- Wire `core` to link against `slang::slang` via `cmake/UseSlang.cmake`
  — today only `tools/slang-smoke` links it; `core/CMakeLists.txt`
  picks it up. Existing 3-tier cache (Slang install-prefix / sccache /
  CMake configure) per ADR 0005 keeps working unchanged.
- Add CI grep clause: `<slang.h>` must not appear under `core/src/rules/`.
- New unit-test TU `tests/unit/test_reflection.cpp` with smoke tests:
  reflect a one-cbuffer / one-binding / one-entry-point shader, assert
  the engine returns a populated `ReflectionInfo`; reflect a known-bad
  shader, assert `Diagnostic` returned with code `clippy::reflection`.
- Optional `--target-profile <profile>` flag wired into
  `cli/src/main.cpp`. When omitted, default per-stage profiles apply.

Effort: ~1 dev week. No rules added — just the engine.

### Sub-phase 3b — shared-utilities PR (sequential, lands second)

Single PR, single agent. Lands:

- `core/src/rules/util/reflect_resource.{hpp,cpp}` per §6.
- `core/src/rules/util/reflect_sampler.{hpp,cpp}` per §6.
- `core/src/rules/util/reflect_stage.{hpp,cpp}` per §6.
- Unit tests for each helper under `tests/unit/util/`.
- Doc-page seeding under `docs/rules/` for the 17 ADR 0011 Phase 3
  rules (status `Pre-v0`; per ADR 0011 §Phase 3 plan rule-doc tasks
  are parallelisable independently of code).

Effort: ~3 dev days. No rules added.

### Sub-phase 3c — parallel rule-pack dispatch

After 3a + 3b land, dispatch up to **5 parallel agents**:

- **Pack A (ADR 0011 PR A)**: buffer-shape rules (ByteAddressBuffer
  alignment, structured-buffer stride, raw-vs-typed buffer choice).
- **Pack B (ADR 0011 PR B)**: sampler / static-sampler / comparison-sampler
  rules.
- **Pack C (ADR 0011 PR C)**: root-signature / compute-pipeline-shape /
  wave-quad-extras rules.
- **Pack D (ADR 0007 §Phase 3)**: the 15 original Phase 3 rules
  (resource-binding, cbuffer-layout, sampler-state, entry-point
  validation).
- **Pack E (ADR 0010 §Phase 3)**: the 23 SM 6.7/6.8/6.9 rules (SER,
  Cooperative Vectors, Long Vectors, OMM, Mesh Nodes — mesh nodes
  gated behind `[experimental] work-graph-mesh-nodes = true` per
  ADR 0010).

Each pack agent works in its own worktree under
`.claude/worktrees/phase3-pack-{A..E}/`, branched from `main`,
merged back via `--no-ff` per CLAUDE.md "Worktree-from-main; close
the merge loop". All 5 packs share only the 3a + 3b surface — no
cross-pack design coupling.

Phase 3 closes when all 5 packs have merged and CI is green on
`main`. Phase 4 implementation work does not start until Phase 3 is
closed (per CLAUDE.md "do not advance from Phase N to Phase N+1 with
unmerged Phase N branches outstanding").

## Consequences

- Slang compile cost is amortised over all reflection-stage rules per
  source per profile within one lint run.
- AST-only lint runs (Phase 0/1/2 rule selection) stay fast — the
  reflection engine never constructs and `<slang.h>` is never touched
  by the AST code path. Verified by a unit test that runs the
  default Phase 1 rule pack and asserts the engine factory was
  never called.
- macOS support remains deferred to Phase 5 per CLAUDE.md "macOS
  CI deferred to Phase 5". Reflection works only on Windows + Linux
  until Phase 5 brings macOS Slang/Metal CI online.
- Public API gains exactly one new header (`reflection.hpp`), one
  new `Stage` value, one new `Rule` virtual, and one new `lint()`
  overload. All additive — no break for current API consumers.
- New CMake dependency: `core` now links against `slang::slang`. The
  3-tier Slang cache (per ADR 0005) already exists and absorbs this
  cleanly; first-time builds without the cache pay the historical
  Slang-from-source cost on the very first `core` build instead of on
  the first `slang-smoke` build.
- The CMake floor is uplifted from `cxx_std_20` to `cxx_std_23`
  **before** sub-phase 3a lands. ADR 0004 already locks C++23; this is
  the natural cut and removes the tracked CLAUDE.md follow-up about
  `CMakeLists.txt still says CMAKE_CXX_STANDARD 20`.
- `tools/slang-smoke/CMakeLists.txt` has its own `CXX_STANDARD 20` —
  uplifted to `23` in the same sub-phase 3a PR.

## Risks & mitigations

- **Risk: Slang ABI churn breaks reflection across version bumps.**
  Mitigation: `cmake/SlangVersion.cmake` already pins
  `HLSL_CLIPPY_SLANG_VERSION` (currently `2026.7.1`) and the per-user
  prebuilt cache is keyed by that string, so a submodule SHA bump
  invalidates stale cache entries automatically. CI runs against the
  pinned version. Bump the pin deliberately in a focused PR with
  `slang_bridge.cpp` updated in lockstep.
- **Risk: Reflection is slow on large shaders.** Mitigation: per-source,
  per-profile cache inside one lint run amortises N-rule reflection
  cost down to one compile per `(SourceId, target_profile)` tuple.
  Hard opt-out via `LintOptions::enable_reflection = false` for users
  who only want AST rules. (The pipeline already short-circuits when
  no enabled rule asks for reflection; this option is for the
  opposite case — reflection rules enabled but the user wants to
  silence them temporarily.)
- **Risk: Multiple entry points per file produce N reflection results.**
  Mitigation: `ReflectionInfo` exposes `entry_points` as a vector;
  rules iterate. The reflection engine produces exactly one
  `ReflectionInfo` per `(SourceId, target_profile)` regardless of
  entry-point count — the per-entry-point split is inside the struct,
  not across struct instances. This keeps the cache key stable and the
  rule API simple.
- **Risk: tree-sitter-hlsl spans don't always align with Slang's symbol
  locations.** Mitigation: rules that need precise byte-spans use the
  `AstTree` as source of truth (the `on_reflection` hook receives
  both); reflection is consulted only for type / binding / layout
  facts. Rules that have no AST anchor available (e.g.
  `cbuffer-padding-hole` synthesising a complaint about a 4-byte
  hole between two named fields) use the cbuffer declaration's
  byte-span recorded in `CBufferLayout::declaration_span`. Never
  invent a synthetic span.

## More Information

- **Cross-references**: ADR 0001 (Slang choice — locked, not
  re-litigated), ADR 0002 (parser — `(SourceId, ByteSpan)` is the
  only span representation that crosses the rule boundary, same
  discipline applies here), ADR 0004 (C++23 baseline — natural
  CMake uplift point), ADR 0005 (CI/CD — Slang cache infrastructure
  reused), ADR 0008 (Phase 1 implementation plan — design template),
  ADR 0009 (Phase 2 implementation plan — shared-utilities-PR +
  parallel-pack dispatch template), ADR 0011 (Phase 3 rule lock
  + 3-pack split this ADR enables).
- **Slang reflection API entry points referenced** (in
  `slang_bridge.cpp` only, never in public headers):
  `IShaderReflection`, `EntryPointReflection`, `VariableLayoutReflection`,
  `TypeLayoutReflection`, `ResourceShape`, `BindingType`. Pinned to
  the `HLSL_CLIPPY_SLANG_VERSION` API surface per ADR 0001.
- **CLAUDE.md "Known issues" interaction**: the
  `cbuffer X : register(b0)` tree-sitter grammar gap means the AST
  cannot tell us the cbuffer's register binding. Fallback noted in
  CLAUDE.md is "Slang reflection knows the resource by name". This
  ADR is the infrastructure that makes that fallback usable from
  rules.

## Open question

Should `target_profile` be a per-source attribute or a per-lint-run
attribute? Today the proposal is **per-lint-run**: `LintOptions`
carries one `target_profile` and every reflection compile in that run
uses it (or the per-stage default if unset). This matches the common
case — a project ships one shader model floor and CI lints against it.

The harder case is a shader library that legitimately compiles against
multiple profiles (e.g. a math header used by both `vs_6_6` and
`ps_6_6`, or a function targeting both DXIL and SPIR-V). Per-source
multi-target reflection would mean each source carries a list of
profiles and the cache key becomes `(SourceId, target_profile)` over
the cross product — straightforward to extend the engine, but
non-trivial for the rule API: should `on_reflection` be invoked once
per profile per source, with the rule iterating? That doubles or
triples rule invocations and changes the diagnostic-deduplication
contract.

This ADR defers that question. The proposed `LintOptions::target_profile`
is per-lint-run; per-source override (probably via a future
`SourceManager::set_target_profiles(SourceId, std::span<std::string>)`
+ a `Rule::on_reflection(..., std::span<const ReflectionInfo>)`
overload) is a tracked follow-up worth its own ADR before any Phase 3
rule depends on it.
