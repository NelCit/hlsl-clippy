---
status: Proposed
date: 2026-05-01
deciders: NelCit
tags: [phase-4, control-flow, cfg, uniformity, dataflow, rule-engine, infrastructure, planning]
---

# Phase 4 control-flow / data-flow infrastructure — CFG + uniformity oracle

## Context and Problem Statement

Phase 4 is the first phase whose rules cannot be expressed as either pure
AST pattern matches (Phase 0/1/2) or a (reflection × AST) join
(Phase 3 — see ADR 0012). Phase 4 rules ask shaped *path-sensitive*
questions over the source — questions like:

- **CFG / reachability**: is `discard` reachable on this path? are these
  two `groupshared` accesses separated by a `GroupMemoryBarrier*` on
  *every* path between them?
- **Uniformity analysis**: is this resource index dynamically uniform
  across the wave? is this branch condition wave-uniform?
- **Loop invariance**: is this expression invariant across all
  iterations of the enclosing loop?
- **Helper-lane state**: is this wave intrinsic invoked from a helper
  lane in PS — i.e. after a `discard` was reachable on some path?
- **Light live-range / def-use**: is this groupshared cell read before
  any thread writes it? is this temporary written and then never read?
- **Quad-uniformity (PS-specific)**: are derivatives valid here, or is
  the calling context not quad-uniform?

Across the locked rule decisions, three ADRs converge on this
infrastructure dependency:

- **ADR 0007 §Phase 4** — 19 rules (loop-invariant sample, cbuffer-load
  in loop, derivative / barrier / wave-intrinsic in divergent CF,
  branch-attribute uniformity, groupshared races + bank conflicts,
  early-Z hazards, helper-lane hazards, ICB divergence, atomic
  pre-reduction, ...).
- **ADR 0010 §Phase 4** — 10 rules (SER coherence + uniformity,
  work-graph atoms, mesh / amplification CFG hazards, derivative-
  uniform-branch replacements).
- **ADR 0011 §Phase 4** — 16 newly LOCKED rules across three packs
  (groupshared microarch, divergence + coherence + mesh, wave-quad
  extras + branch hints).

That is **~45 rules blocked on a single piece of infrastructure that
has not yet been designed**: a way to expose a control-flow graph plus
a uniformity oracle plus a *light* data-flow facility to the lint
engine, without violating the constraints already locked elsewhere in
the codebase:

1. **Public headers under `core/include/hlsl_clippy/` MUST NOT leak
   `<tree_sitter/api.h>` or `<slang.h>`.** CI grep enforces this.
   Rules see `(SourceId, ByteSpan)` only — never `TSNode`. Same
   discipline applies to any CFG-node identifier crossing the
   boundary. See ADR 0002 + CLAUDE.md "What NOT to do".
2. **No exceptions across the `core` API boundary.** CFG-build
   failures and uniformity-analysis bailouts surface as
   `std::expected<T, Diagnostic>`. See ADR 0004 + CLAUDE.md
   "Code standards — Ban list".
3. **AST-only and Reflection-only lint runs (Phase 0/1/2/3 rule
   selection) must stay fast.** Linting a 50-rule shader against the
   math-pack or the reflection-aware buffer-pack should not pay CFG
   construction cost.
4. **No hard dependency on Slang IR.** Slang's IR is reserved for
   Phase 7 (register pressure, redundant samples, packed-math
   precision). Phase 4 must work over the tree-sitter AST so it can
   ship without IR plumbing and so its rules surface portable
   anti-patterns rather than IR-level micro-optimisations.

This ADR proposes the smallest viable architecture that satisfies all
four constraints, and lays out the sub-phase order in which the
infrastructure lands so the five Phase 4 rule packs (ADR 0011 §Phase 4
plan + ADR 0007 §Phase 4 + ADR 0010 §Phase 4) can dispatch in parallel
against it. **No code is written by this ADR.** It is a plan, in the
same shape as ADR 0008 (Phase 1), ADR 0009 (Phase 2), and ADR 0012
(Phase 3).

Phase 4 implementation cannot start until this ADR's sub-phase 4a +
4b have landed. Phase 4 is *not* gated on Phase 3 / ADR 0012: the CFG
infrastructure described here works without reflection. Where a
specific rule needs entry-point stage information (helper-lane
analysis in PS), it falls back to a conservative "treat as possibly
PS" answer when reflection is unavailable.

## Decision Drivers

- **Build the CFG over the existing tree-sitter AST.** Do not take a
  hard dependency on Slang IR. Slang IR is Phase 7 territory and
  Phase 4 rules deliberately target patterns visible at the source
  level. The CFG builder walks the same `AstTree` rules already
  consume; it does not require a Slang compile.
- **Stable spans.** CFG nodes carry the same `(SourceId, ByteSpan)`
  mapping as AST nodes. CFG-derived diagnostics anchor to the
  underlying source span — never a synthetic "block start" token.
  Where a CFG fact spans multiple statements (a missing barrier
  between a write and a read), the rule emits the *primary* span on
  the offending second access and references the dominating
  statement via a `note` diagnostic.
- **`std::expected<ControlFlowInfo, Diagnostic>` at the API
  boundary.** CFG-build failures (e.g. a function whose subtree
  contains a tree-sitter ERROR node — see ADR 0002 known-grammar
  gaps) surface as a `Diagnostic` with `code = "clippy::cfg"`,
  `severity = Severity::Warning`, anchored to the function
  declaration's byte-span. The function is then skipped for
  CFG-stage rules — they don't crash, they just don't fire.
- **Lazy invocation.** A lint run that selects only Phase 0/1/2/3
  rules MUST NOT trigger CFG construction. The pipeline inspects
  every enabled rule's `stage()` and only constructs the
  `CfgEngine` if at least one rule has `stage() == Stage::ControlFlow`.
- **Uniformity is taint-style propagation from known-divergent
  sources.** Seeded by `SV_DispatchThreadID`, `SV_GroupThreadID`,
  `SV_GroupIndex`, `WaveGetLaneIndex()`, dynamic loop counters, and
  (when reflection from ADR 0012 is available) resource indices
  flagged `NonUniform`. Everything else flows from operands. The
  oracle is best-effort: false-positive uniformity (saying a value
  is divergent when it is in fact dynamically uniform) is acceptable
  and surfaces as a *warn*-severity diagnostic, never *error*.
- **Bounded inter-procedural reasoning.** Most Phase 4 rules are
  intra-procedural. The CFG engine supports limited inter-procedural
  reasoning by inlining function-call effects into the caller's CFG,
  bounded to a configurable depth (default 3). Rules that need
  deeper reasoning are explicitly Phase 7 (register-pressure /
  IR-level).
- **ERROR-node tolerance.** The tree-sitter-hlsl grammar produces
  ERROR nodes for several known constructs (per ADR 0002 +
  `external/treesitter-version.md`: `cbuffer X : register(b0)`,
  some `[numthreads]` + function-decl combos, struct-member
  semantics edge cases). The CFG builder skips functions whose
  subtree contains an ERROR node and surfaces a single
  `clippy::cfg` diagnostic per skipped function. Rules opt into
  CFG-based diagnostics by overriding `Rule::on_cfg`; the
  orchestrator never invokes that hook against an ERROR-tainted
  function.
- **Cacheable.** Building the CFG + dominator tree + uniformity
  taint for the same `SourceId` twice in a single lint run reuses
  the cached result. Cache lifetime is the lint-run, not the
  process; per-run construction is amortised across all Phase 4
  rules invoked on that source.

## Considered Options

### Option A — Reuse Slang IR for CFG

Query Slang's IR for control flow. The IR carries explicit basic
blocks, dominators, and (with extension) lane-uniformity attributes.
Hook a "CFG bridge" into ADR 0012's reflection engine and walk the
IR rather than the AST.

- Good: Slang's IR-level CFG is already correct (no tree-sitter
  grammar gaps to work around).
- Good: Slang's lane-uniformity tracking is plausibly more accurate
  than a fresh taint-propagation pass over the AST.
- Bad: pulls IR-level coupling into Phase 4, which we deliberately
  separated from Phase 7. Once Phase 4 takes a Slang-IR dependency,
  every Phase 4 rule pays Slang compile cost — exactly the
  fast-AST-runs invariant that ADR 0012 worked hard to preserve.
- Bad: ties Phase 4 to ADR 0012 / Phase 3 reflection landing first.
  A Phase 4 rule pack would not be able to dispatch until both
  reflection and IR plumbing landed. The whole point of Phase 4 is
  rules that catch CFG hazards visible at the source level.
- Bad: Slang's IR-level basic-block boundaries do not in general
  preserve a 1:1 mapping to source byte-spans. Diagnostics would
  need a reverse-mapping table from IR location to source span,
  which is itself non-trivial.

**Rejected.**

### Option B — Build CFG from tree-sitter AST (chosen)

Walk the `AstTree`, identify basic blocks at well-known boundaries
(function entry, branch successors, loop header, loop tail,
post-`discard`, post-barrier, function exits), and edge-out to
produce an intra-procedural CFG. Build a dominator tree on top.
Run a fixed-point taint-propagation pass for uniformity, seeded by
known-divergent sources. Bound inter-procedural reasoning by
inlining call effects to a configurable depth (default 3).

- Good: minimal new infrastructure. Pure C++ over the same
  `(SourceId, ByteSpan)` model rules already use. No new external
  deps.
- Good: decouples Phase 4 from Slang. Phase 4 work can start the
  moment 4a + 4b land, regardless of ADR 0012's status.
- Good: AST-only / reflection-only lint runs pay zero CFG cost
  (engine never constructs, lazy by `stage()`).
- Good: span model is stable. Every CFG-derived diagnostic anchors
  to a real source byte-span via the underlying AST node.
- Bad: tree-sitter ERROR nodes are real (per ADR 0002). Mitigated
  by per-function ERROR-tolerance + a single `clippy::cfg` skip
  diagnostic; rules don't crash, they just don't fire on broken
  subtrees.
- Bad: AST-level uniformity is best-effort and admits
  false-positives on dynamically-uniform-but-not-statically-provable
  values (e.g. `cbuffer.flag * laneId` where `flag` is 0 or 1).
  Mitigated by warn-severity contract + suppression annotations.

**Chosen.**

### Option C — Hybrid: AST CFG + optional Slang IR refinement

Ship Option B as the baseline, then layer an optional Slang-IR
refinement pass for hard cases (e.g. uniformity facts the AST taint
pass cannot prove; loop-invariance across a function-call boundary).
Behind a `LintOptions::cfg_use_ir_refinement = false` knob.

- Good: Option B's invariants in the default path; Option A's
  precision on demand for users who already pay reflection cost.
- Bad: doubles the CFG surface (AST + IR-bridge). Risk of design
  drift between the two passes; risk of rules that fire under one
  but not the other.
- Bad: too big for one ADR. The IR-refinement design is non-trivial
  and would block this ADR's primary purpose (unblock Phase 4
  rules) on a multi-pass design discussion.

**Defer to a follow-up ADR.** Option B (AST CFG only) is the
*minimum* viable Phase 4 infrastructure; an IR-refinement layer is
worth its own ADR once Phase 4 rules have shipped and surfaced real
gaps.

## Decision Outcome

**Option B — AST-level CFG + uniformity oracle.**

The proposed architecture, broken into the six concrete API additions
that constitute it.

### 1. New public header `core/include/hlsl_clippy/control_flow.hpp` (opaque types only)

No `<tree_sitter/api.h>` include. No `<slang.h>` include. Pure value
types. Sketch:

```cpp
namespace hlsl_clippy {

enum class Uniformity : std::uint8_t {
    Unknown,
    Uniform,        // wave-uniform / dynamically uniform
    Divergent,      // varies across lanes
    LoopInvariant,  // uniform-per-iteration; tracked separately for
                    // loop-hoist-style rules
};

enum class HelperLaneState : std::uint8_t {
    NotApplicable,    // not in PS, or pre-discard on this path
    PossiblyHelper,   // PS, post-discard reachable
};

/// Opaque handle into a lint-run-scoped CFG. Cheap to copy.
class BasicBlockId {
public:
    constexpr BasicBlockId() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }
    [[nodiscard]] friend constexpr bool operator==(
        BasicBlockId, BasicBlockId) noexcept = default;

private:
    friend class CfgEngine;
    explicit constexpr BasicBlockId(std::uint32_t v) noexcept : value_(v) {}
    std::uint32_t value_ = 0;
};

/// Per-CFG-node summary, anchored to the underlying AST byte-span.
struct CfgNodeInfo {
    Span span;
    BasicBlockId block;
    Uniformity reach_uniformity{Uniformity::Unknown};
    HelperLaneState helper_lane_state{HelperLaneState::NotApplicable};
};

/// Whole-function CFG summary. One per function in the source.
struct CfgInfo {
    std::vector<BasicBlockId> blocks;
    Span entry_span{};
    Span function_span{};

    // Reachability / dominator / barrier helpers used by Phase 4 rules.
    [[nodiscard]] bool reachable_from(
        BasicBlockId from, BasicBlockId to) const noexcept;
    [[nodiscard]] bool dominates(
        BasicBlockId a, BasicBlockId b) const noexcept;
    [[nodiscard]] bool barrier_between(
        BasicBlockId a, BasicBlockId b) const noexcept;
    [[nodiscard]] bool any_path_lacks_barrier(
        BasicBlockId from, BasicBlockId to) const noexcept;
};

/// Best-effort uniformity oracle keyed by source byte-spans.
struct UniformityOracle {
    [[nodiscard]] Uniformity of_expr(Span expr) const noexcept;
    [[nodiscard]] Uniformity of_branch(Span branch_stmt) const noexcept;
    [[nodiscard]] Uniformity of_loop_iv(Span loop_stmt) const noexcept;
};

/// Top-level info handed to `Rule::on_cfg`. Aggregates the per-function
/// CFGs, the uniformity oracle, and any whole-source helper-lane facts.
struct ControlFlowInfo {
    std::vector<CfgInfo> functions;          // one per function in source
    UniformityOracle    uniformity{};

    // Convenience queries used by Phase 4 rules.
    [[nodiscard]] const CfgInfo* function_containing(
        Span span) const noexcept;
    [[nodiscard]] HelperLaneState helper_lane_state_at(
        Span span) const noexcept;
};

}  // namespace hlsl_clippy
```

Every type is a copyable / movable value. `BasicBlockId` is an opaque
handle; the engine owns the actual block storage. No `unique_ptr`s
to opaque tree-sitter or Slang handles cross the public boundary.

### 2. Extend `Stage` enum in `core/include/hlsl_clippy/rule.hpp`

```cpp
enum class Stage : std::uint8_t {
    Ast,           // AST-only (default; Phase 0/1/2)
    Reflection,    // needs ReflectionInfo (Phase 3 — ADR 0012)
    ControlFlow,   // needs ControlFlowInfo (Phase 4)
    // Future: Ir (Phase 7).
};
```

The default `Rule::stage()` continues to return `Stage::Ast`, so all
existing Phase 0/1/2/3 rules keep their current behaviour with zero
source change.

### 3. New rule entrypoint `Rule::on_cfg`

Add a fourth virtual alongside `on_node`, `on_tree`, and (per ADR
0012) `on_reflection`:

```cpp
class Rule {
    // ... existing methods unchanged ...

    /// Control-flow-stage hook. Called once per source with the
    /// already-built `ControlFlowInfo`. Default implementation does
    /// nothing.
    virtual void on_cfg(const AstTree& tree,
                        const ControlFlowInfo& cfg,
                        RuleContext& ctx);
};
```

Rules with `stage() == Stage::ControlFlow` override this. CFG-stage
rules retain access to the `AstTree` because most of them want both
sides — the CFG tells them *what path*, the AST tells them *what
syntactic shape*. The orchestrator never calls `on_cfg` for non-CFG
rules and never calls AST/reflection hooks for `Stage::ControlFlow`
rules unless the rule explicitly opts in by overriding multiple
hooks (legitimate: a rule that does AST-side syntactic filtering and
CFG-side path validation in one body).

### 4. Internal `CfgEngine` (lives in `core/src/control_flow/`)

New private module:

```
core/src/control_flow/
    engine.hpp                // public-to-core API (returns ControlFlowInfo)
    engine.cpp                // orchestration + lint-run cache
    cfg_builder.hpp/cpp       // walk AST, emit basic blocks + edges
    dominators.hpp/cpp        // Lengauer-Tarjan dominator tree
    uniformity_analyzer.hpp/cpp  // taint propagation
    helper_lane_analyzer.hpp/cpp // PS-only post-discard reachability
```

Responsibilities of `CfgEngine`:

- **Walk the `AstTree`** for the source and identify per-function
  basic-block boundaries. Boundaries are introduced at: function
  entry, after every branch (`if` / `switch` arms), at loop header,
  before loop tail, post-`discard`, post-`clip`, post-barrier
  (`GroupMemoryBarrier*`, `DeviceMemoryBarrier*`), function exits
  (explicit `return`, fall-through to closing brace).
- **Build a dominator tree** per function via the standard
  Lengauer-Tarjan algorithm. Used for `dominates()` and
  `barrier_between()` queries.
- **Run uniformity taint propagation** seeded by:
  - `SV_DispatchThreadID`, `SV_GroupThreadID`, `SV_GroupIndex`,
    `SV_PrimitiveID`, `SV_VertexID`, `SV_InstanceID` →
    `Divergent`.
  - `WaveGetLaneIndex()`, `WaveGetLaneCount()` (lane index only) →
    `Divergent` / `Uniform` respectively.
  - When ADR 0012 reflection is available: resource indices for
    bindings flagged `NonUniform` → `Divergent`.
  - Loop induction variables → `LoopInvariant` within the loop;
    `Divergent` outside.
  - Function parameters: assumed `Divergent` at the call site
    unless the call is inlined (per `cfg_inlining_depth`).
  - Operands flow: an expression's `Uniformity` is the join
    (`Divergent` ⊕ anything = `Divergent`; `LoopInvariant` ⊕
    `Uniform` = `LoopInvariant`; `Uniform` ⊕ `Uniform` = `Uniform`).
  - Inter-procedural fan-out is bounded by
    `LintOptions::cfg_inlining_depth` (default 3); deeper calls are
    treated as a black box and their return values default to
    `Unknown`.
- **Run helper-lane analysis** on PS entry points only. A program
  point is `PossiblyHelper` if `discard` or `clip` is reachable on
  some path from function entry to that point. Outside PS or before
  the first reachable `discard`, the state is `NotApplicable`. When
  reflection is unavailable to confirm PS stage, the analyzer
  conservatively treats every function as possibly-PS.
- **Tolerate ERROR nodes.** A function whose tree-sitter subtree
  contains an ERROR node is skipped: no CFG entry is added to
  `ControlFlowInfo::functions` for it, and a single
  `clippy::cfg` warn-severity diagnostic anchored to the function
  declaration's span is appended to the lint output.
- **Return `std::expected<ControlFlowInfo, Diagnostic>`.** A hard
  failure (e.g. the entire source is unparseable) returns
  `std::unexpected`. Per-function ERROR-tolerance does *not*
  promote to a hard failure.
- **Per-lint-run cache** keyed by `SourceId`. Cache uses
  `std::flat_map` per CLAUDE.md "C++23 idioms to use". Cache
  lifetime is one `lint()` invocation.

### 5. Extended `LintOptions` (additive on top of ADR 0012)

Additive change. The `LintOptions` struct introduced by ADR 0012
gains two CFG-related fields:

```cpp
struct LintOptions {
    // --- existing fields (ADR 0012) ---
    std::optional<std::string> target_profile;
    bool                       enable_reflection      = true;
    std::uint32_t              reflection_pool_size   = 4;

    // --- new fields (this ADR) ---

    /// When false, the control-flow stage is skipped entirely even
    /// if CFG-stage rules are enabled. Useful for fast iteration /
    /// AST-only smoke runs / tests that want to isolate AST
    /// behaviour without disabling reflection rules.
    bool          enable_control_flow = true;

    /// Maximum inter-procedural inlining depth for the uniformity
    /// analyzer. Default 3. Higher values produce more precise
    /// uniformity facts at higher build-time cost; rules that need
    /// deeper reasoning are explicitly Phase 7.
    std::uint32_t cfg_inlining_depth = 3;
};
```

The pipeline inspects every enabled rule's `stage()` once at the
start of `lint()`. If no rule has `stage() == Stage::ControlFlow`,
the `CfgEngine` is never constructed and `cfg_inlining_depth` /
`enable_control_flow` are silently ignored. This is what keeps
AST-only and reflection-only runs fast.

### 6. Shared utilities for Phase 4 rules

Per ADR 0009 / ADR 0011 / ADR 0012's pattern, three shared-utility
headers in `core/src/rules/util/`:

- **`cfg_query.hpp/cpp`** — common queries used by groupshared,
  mesh, and divergence rules:
  - `reachable_with_discard(cfg, from, to)`
  - `barrier_separates(cfg, write_span, read_span)`
  - `any_path_lacks_call(cfg, from, callee)` (used by
    `dispatchmesh-not-called`)
  - `loop_containing(cfg, span)`
  - `enclosing_branch_has_attribute(cfg, span, attr)` (used by
    `branch-on-uniform-missing-attribute`,
    `flatten-on-uniform-branch`, `forcecase-missing-on-ps-switch`)
- **`uniformity.hpp/cpp`** — divergent-source taint helpers used by
  divergence and wave-intrinsic rules:
  - `is_divergent(oracle, span)`
  - `is_dynamically_uniform(oracle, span)` (Uniform OR
    LoopInvariant outside loop)
  - `enclosing_branch_uniformity(cfg, oracle, span)`
  - `wave_intrinsic_call_at(span)` and the matching uniformity
    test for divergent-CF wave-intrinsic rules
- **`light_dataflow.hpp/cpp`** — read-before-write,
  write-without-read, loop-invariance helpers used by groupshared
  rules + cbuffer-load-in-loop:
  - `groupshared_first_access_kind(cfg, span)` (read or write)
  - `groupshared_dead_store(cfg, span)`
  - `loop_invariant(cfg, oracle, expr_span, loop_span)`

These are private headers; rules `#include "rules/util/..."` and
never see CFG-internal types directly.

## Implementation sub-phases

Mirrors ADR 0009 / ADR 0011 / ADR 0012's "infra PR + shared-utilities
PR + parallel category packs" pattern. **Do not parallelise sub-phases
4a and 4b** — they share design surface and serialising them avoids
merge-time drift. Sub-phase 4c is the parallel-pack dispatch.

### Sub-phase 4a — infrastructure PR (sequential, must land first)

Single PR, single agent. Lands:

- `core/include/hlsl_clippy/control_flow.hpp` (opaque types per §1).
- New private module `core/src/control_flow/` (engine, cfg_builder,
  dominators, uniformity_analyzer, helper_lane_analyzer per §4).
- Extend `Stage` enum (§2) and `Rule::on_cfg` virtual (§3).
- Extend `LintOptions` with the two CFG fields (§5). Existing fields
  (and existing `lint()` overloads) preserved.
- Wire `CfgEngine` into the orchestrator: stage-dispatch logic
  inspects rules' `stage()`, lazily constructs the engine, dispatches
  `on_cfg` against the cached `ControlFlowInfo`.
- New unit-test TU `tests/unit/test_cfg.cpp` with smoke tests:
  - CFG of a straight-line function has 1 block.
  - CFG of an `if`/`else` has the expected dominator structure.
  - Loop CFG identifies the header / latch correctly.
  - Discard reachability flags the correct PS post-discard region.
  - ERROR-node tolerance: a function with a known grammar gap is
    skipped, lint output contains exactly one `clippy::cfg`
    diagnostic, lint does not crash.
- New unit-test TU `tests/unit/test_uniformity.cpp` with smoke
  tests:
  - `SV_DispatchThreadID` flagged divergent.
  - `cbuffer.f` flagged uniform.
  - Loop induction variable flagged loop-invariant inside the loop.
  - Inter-procedural inlining at depth 3 propagates divergence
    through three call levels but stops at the fourth.
- Optional `--cfg-inlining-depth <N>` flag wired into
  `cli/src/main.cpp`.

Effort: **~2 dev weeks.** CFG construction + Lengauer-Tarjan
dominator tree + uniformity taint propagation + helper-lane analysis
is non-trivial; this is the largest single infrastructure PR in the
project so far. No rules added — just the engine.

### Sub-phase 4b — shared-utilities PR (sequential, lands second)

Single PR, single agent. Lands:

- `core/src/rules/util/cfg_query.hpp/cpp` per §6.
- `core/src/rules/util/uniformity.hpp/cpp` per §6.
- `core/src/rules/util/light_dataflow.hpp/cpp` per §6.
- Unit tests for each helper under `tests/unit/util/`.
- Doc-page seeding under `docs/rules/` for the 16 ADR 0011 Phase 4
  rules + 19 ADR 0007 §Phase 4 rules + 10 ADR 0010 §Phase 4 rules
  (status `Pre-v0`; per ADR 0011 §Phase 4 plan, doc tasks are
  parallelisable independently of code).

Effort: **~3 dev days.** No rules added.

### Sub-phase 4c — parallel rule-pack dispatch

After 4a + 4b land, dispatch up to **5 parallel agents**:

- **Pack A — ADR 0011 PR A — groupshared microarch** (5 rules):
  `groupshared-stride-non-32-bank-conflict`,
  `groupshared-dead-store`, `groupshared-overwrite-before-barrier`,
  `groupshared-atomic-replaceable-by-wave`,
  `groupshared-first-read-without-barrier`.
- **Pack B — ADR 0011 PR B — divergence + coherence + mesh**
  (6 rules): `divergent-buffer-index-on-uniform-resource`,
  `rwbuffer-store-without-globallycoherent`,
  `primcount-overrun-in-conditional-cf`, `dispatchmesh-not-called`,
  `clip-from-non-uniform-cf`,
  `precise-missing-on-iterative-refine`.
- **Pack C — ADR 0011 PR C — wave-quad extras + branch hints**
  (5 rules): `manual-wave-reduction-pattern`,
  `quadany-quadall-opportunity`,
  `wave-prefix-sum-vs-scan-with-atomics`,
  `flatten-on-uniform-branch`,
  `forcecase-missing-on-ps-switch`.
- **Pack D — ADR 0007 §Phase 4** (19 rules):
  `loop-invariant-sample`, `cbuffer-load-in-loop`,
  `redundant-computation-in-branch`, `derivative-in-divergent-cf`,
  `barrier-in-divergent-cf`, `wave-intrinsic-non-uniform`,
  `branch-on-uniform-missing-attribute`, `small-loop-no-unroll`,
  `discard-then-work`, `groupshared-uninitialized-read`,
  `sample-in-loop-implicit-grad`,
  `early-z-disabled-by-conditional-discard`,
  `wave-intrinsic-helper-lane-hazard`,
  `wave-active-all-equal-precheck`, `cbuffer-divergent-index`,
  `interlocked-bin-without-wave-prereduce`,
  `interlocked-float-bit-cast-trick`,
  `groupshared-stride-32-bank-conflict`,
  `groupshared-write-then-no-barrier-read`.
- **Pack E — ADR 0010 §Phase 4** (10 rules): SER coherence,
  uniformity, work-graph atoms, mesh / amplification CFG hazards,
  derivative-uniform-branch replacements, per ADR 0010 §Phase 4
  list. Mesh-node preview rules remain gated behind
  `[experimental] work-graph-mesh-nodes = true` per ADR 0010.

Each pack agent works in its own worktree under
`.claude/worktrees/phase4-pack-{A..E}/`, branched from `main`,
merged back via `--no-ff` per CLAUDE.md "Worktree-from-main; close
the merge loop". All 5 packs share only the 4a + 4b surface — no
cross-pack design coupling.

Phase 4 closes when all 5 packs have merged and CI is green on
`main`. Phase 5 (LSP) implementation work does not start until
Phase 4 is closed (per CLAUDE.md "do not advance from Phase N to
Phase N+1 with unmerged Phase N branches outstanding").

## Consequences

- AST-only and reflection-only lint runs (Phase 0/1/2/3 rule
  selection) stay fast — the `CfgEngine` never constructs and CFG
  construction cost is never paid. Verified by a unit test that
  runs the default Phase 1 + Phase 3 rule pack and asserts the
  engine factory was never called.
- Phase 4 work can start the moment 4a + 4b land and **does NOT
  depend on ADR 0012 / Phase 3 reflection landing first**. Where
  reflection facts would tighten a Phase 4 rule (e.g. divergent
  resource index annotated `NonUniform`), the rule degrades
  gracefully when `enable_reflection = false`: it loses precision
  on uniformity but does not produce false-error diagnostics.
- No new external deps. CFG infrastructure is pure C++ over
  existing tree-sitter spans. The 3-tier Slang cache (per ADR
  0005) is unaffected; first-time builds without the cache pay no
  new third-party cost.
- Public API gains exactly one new header (`control_flow.hpp`),
  one new `Stage` value, one new `Rule` virtual, and two new
  `LintOptions` fields. All additive — no break for current API
  consumers.
- Phase 4's uniformity oracle is best-effort. Rules anchor at
  warn-severity unless the underlying property is statically
  unambiguous (e.g. `barrier-in-divergent-cf` is undefined
  behaviour when the branch is genuinely divergent — but the rule
  ships at warn until the false-positive rate has been measured
  against the corpus).
- The CFG builder's call-graph fan-out is bounded by
  `cfg_inlining_depth` (default 3). Rules that need deeper
  inter-procedural reasoning are explicitly Phase 7 (register
  pressure / IR-level). The `cfg_inlining_depth` knob is exposed
  in case projects with deep helper-function nesting want to opt
  into more precision.
- macOS support remains deferred to Phase 5 per CLAUDE.md "macOS
  CI deferred to Phase 5". CFG infrastructure is platform-neutral
  and works on Linux + Windows from day one.

## Risks & mitigations

- **Risk: tree-sitter-hlsl ERROR nodes break CFG construction.**
  ADR 0002 records several known grammar gaps
  (`cbuffer X : register(b0)`, some `[numthreads]` combos,
  struct-member semantics edge cases). Mitigation: per-function
  ERROR-tolerance — the CFG builder skips functions whose subtree
  contains an ERROR node, surfaces a single warn-severity
  `clippy::cfg` diagnostic anchored to the function span, and
  rules opt out of CFG-based diagnostics on that function. The
  ERROR-tolerance path is unit-tested in sub-phase 4a's
  `tests/unit/test_cfg.cpp`. Patches to upstream tree-sitter-hlsl
  are welcome and remove this skip path one gap at a time.

- **Risk: uniformity false-positives on dynamically-uniform-but-
  not-statically-provable values.** `cbuffer.flag * laneId` where
  `flag` is 0 or 1 at runtime is dynamically uniform but the AST
  taint pass cannot prove it. Mitigation: rules treat uniformity
  as best-effort and ship at *warn* severity. The
  `// hlsl-clippy: ignore <rule-id>` inline-suppression mechanism
  (per ADR 0008) lets authors annotate provably-uniform-by-runtime
  patterns without masking the wider rule. The Option C hybrid (AST
  CFG + Slang IR refinement) is the documented escape hatch if
  false-positive rates prove too high in practice.

- **Risk: CFG builder cost on large shaders.** A 5,000-line
  shader with deep loop nesting + several entry points may be
  slow to build a CFG for. Mitigation: per-source cache (one CFG
  build per source per lint run); hard opt-out via
  `LintOptions::enable_control_flow = false`; the pipeline
  short-circuits when no enabled rule asks for CFG, so the
  cost is paid only when CFG-stage rules are enabled.

- **Risk: helper-lane analysis interaction with Phase 3 reflection.**
  Helper-lane state requires knowing the entry point's stage (PS
  vs CS vs VS), which tree-sitter alone cannot reliably tell us
  (`[shader("pixel")]` plus the function name vs an entry-point
  tag in reflection). Mitigation: when ADR 0012 reflection is
  available the helper-lane analyzer uses it to scope analysis to
  PS entry points only; when reflection is unavailable the
  analyzer falls back to "treat every function as possibly-PS",
  which is conservative (more `PossiblyHelper` than necessary)
  but never *incorrect* (no false-`NotApplicable` claims that
  would let a real bug through).

## More Information

- **Cross-references**:
  - ADR 0001 (Slang choice — locked, not re-litigated; this ADR
    deliberately does *not* take a Slang IR dependency).
  - ADR 0002 (tree-sitter-hlsl parser; known grammar gaps drive
    the ERROR-tolerance design).
  - ADR 0004 (C++23 baseline — `std::expected`,
    `std::flat_map` used throughout the engine).
  - ADR 0005 (CI/CD — no new deps, no new cache tiers needed).
  - ADR 0007 §Phase 4 (19 rules unblocked by this ADR).
  - ADR 0008 (Phase 1 implementation plan — design template).
  - ADR 0009 (Phase 2 implementation plan — shared-utilities-PR +
    parallel-pack dispatch template).
  - ADR 0010 §Phase 4 (10 rules unblocked by this ADR; mesh-node
    rules remain experimental-gated).
  - ADR 0011 §Phase 4 (16 LOCKED rules + the 3-pack split this
    ADR enables).
  - ADR 0012 (Phase 3 reflection infrastructure — sibling
    infrastructure ADR that this ADR mirrors structurally; this
    ADR's `Stage::ControlFlow` extension preserves ADR 0012's
    `Stage::Reflection` extension).
- **Algorithmic references** (used in `core/src/control_flow/` only,
  never leaked through public headers):
  - Lengauer-Tarjan dominator-tree algorithm
    (Lengauer & Tarjan, 1979).
  - Standard taint-propagation fixed-point iteration over the
    SSA-like join-lattice {Unknown ⊑ Uniform, LoopInvariant ⊑
    Divergent}.
- **CLAUDE.md "Known issues" interaction**: the
  `cbuffer X : register(b0)` grammar gap and the
  `[numthreads(...)]` + function-decl combo gap mean some
  functions will produce ERROR nodes. The CFG ERROR-tolerance
  path is the runtime mitigation; upstream grammar patches are
  the long-term fix.
- **Coverage gate impact**: per CLAUDE.md, the coverage target
  is 80% on `core/` by Phase 4 completion. The CFG infrastructure
  is unit-tested in sub-phase 4a; the shared utilities are
  unit-tested in sub-phase 4b; each Phase 4 rule pack ships with
  fixtures + Catch2 tests per the standard new-rule checklist.

## Open question

Should `cfg_inlining_depth` be a per-project knob in
`.hlsl-clippy.toml`, a per-rule knob, or a per-lint-run option only?
Today the proposal is **per-lint-run only** (via `LintOptions`).
This matches the simplest case — a project picks one inlining depth
and every CFG-stage rule uses it.

The harder case is a project that legitimately wants different
inlining depths per rule: e.g. `barrier-in-divergent-cf` wants
high precision (depth 5+) because the diagnostic is severe, while
`small-loop-no-unroll` is happy with depth 1 because it is purely
syntactic. Per-rule depth would mean the engine builds the CFG
multiple times per source per run (once per distinct depth), or
caches CFGs at the deepest requested depth and slices uniformity
facts at shallower depths.

This ADR defers that question. The proposed
`LintOptions::cfg_inlining_depth` is per-lint-run; per-rule depth
override (probably via an extension to `Config` and a new
`Rule::cfg_inlining_depth_override()` virtual) is a tracked
follow-up worth its own ADR before any Phase 4 rule depends on it.
