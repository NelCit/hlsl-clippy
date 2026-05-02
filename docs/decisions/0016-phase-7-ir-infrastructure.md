---
status: Proposed
date: 2026-05-02
decision-makers: maintainers
consulted: ADR 0012, ADR 0013
tags: [phase-7, ir, dxil, spirv, register-pressure, liveness, ray-tracing, mesh, infrastructure, planning]
---

# Phase 7 IR-level analysis infrastructure — DXIL/SPIR-V consumer engine + liveness + register-pressure estimator

## Context and Problem Statement

Phase 7 (ROADMAP.md § "Phase 7 — Stretch / research") is the first
phase whose rules cannot be answered from any combination of the
AST (Phase 0/1/2), Slang reflection (Phase 3 — ADR 0012), or the
AST-CFG + uniformity oracle (Phase 4 — ADR 0013). Phase 7 rules ask
*post-codegen* questions over the IR Slang produces:

- **Memory / register pressure**: `vgpr-pressure-warning`,
  `scratch-from-dynamic-indexing`, `redundant-texture-sample`,
  `groupshared-when-registers-suffice`,
  `buffer-load-width-vs-cache-line` — all need a per-target IR
  basic-block view, per-instruction live-value counts, and a
  per-architecture VGPR/SGPR width estimate.
- **Precision / packing**: `min16float-opportunity`,
  `unpack-then-repack`, `manual-f32tof16` — pattern matches over
  arithmetic ops at IR level (where Slang's HLSL frontend has
  already lowered intrinsics to opcodes).
- **Ray tracing**: `oversized-ray-payload`,
  `missing-accept-first-hit`, `recursion-depth-not-declared`,
  `live-state-across-traceray` — DXR-specific instructions visible
  only at IR level.
- **Mesh / amplification**: `meshlet-vertex-count-bad`,
  `output-count-overrun` — mesh-shader output instructions and
  count constants visible only at IR level.

Plus the SM 6.9 cross-phase rule from ADR 0010 §"Risks":
- `maybereorderthread-without-payload-shrink` — shares
  `live-state-across-traceray`'s liveness machinery.

That is **15 rules blocked on a single piece of infrastructure that
has not yet been designed**: a way to consume Slang's compiled IR
output in the lint engine without violating the constraints already
locked by ADRs 0001/0002/0004/0005/0012/0013:

1. **Public headers under `core/include/hlsl_clippy/` MUST NOT leak
   `<slang.h>`, `<dxil_*.h>`, or `<spirv-tools/*.h>`.** CI grep
   enforces the Slang case today; the IR engine adds DXIL/SPIR-V
   parser headers to that grep clause. Rules see opaque
   `IrFunction` / `IrInstruction` / `IrBasicBlock` value types with
   stable `(SourceId, ByteSpan)` anchors, never raw IR handles.
2. **No exceptions across the `core` API boundary.** IR parsing
   failures (truncated DXIL container, unsupported SPIR-V
   extension) surface as `std::expected<IrInfo, Diagnostic>` with
   `code = "clippy::ir"`, `severity = Severity::Warning`, anchored
   to `(source, byte 0..0)`. AST + reflection + CFG rules continue
   to fire; only IR-stage rules are skipped for the affected source.
3. **AST / reflection / CFG-only lint runs (Phase 0/1/2/3/4 rule
   selection) must stay fast.** Linting against the math-pack must
   not invoke Slang code generation. The pipeline inspects every
   enabled rule's `stage()` and only constructs `IrEngine` if at
   least one rule has `stage() == Stage::Ir`.
4. **No new build-time mandatory dependency for AST-only users.**
   Phase 7 introduces a parser dependency (DXIL reader + spirv-tools
   per the chosen option below). It is link-time conditional under
   a CMake option `HLSL_CLIPPY_ENABLE_IR=ON` (default `ON` for the
   v0.7 release build, `OFF` for downstream consumers who only want
   the AST + reflection + CFG surface).

This ADR proposes the smallest viable architecture that satisfies
all four constraints, and lays out the sub-phase order in which the
infrastructure lands so the four Phase 7 rule packs can dispatch in
parallel against it. **No code is written by this ADR.** It is a
plan, in the same shape as ADR 0012 (Phase 3) and ADR 0013
(Phase 4).

Phase 7 implementation cannot start until this ADR's sub-phase 7a +
7b have landed. Phase 7 *does* depend on Phase 3 / ADR 0012: the IR
engine reuses the existing `slang_bridge.cpp` `ISession` pool to
emit IR (today reflection alone is requested; this ADR adds the
`getEntryPointCode` / `getTargetCode` calls). Phase 7 does *not*
depend on Phase 4 / ADR 0013 — IR has its own basic-block structure
distinct from the AST CFG.

## Decision Drivers

- **Reuse the existing Slang `ISession` pool from ADR 0012.** Today
  `core/src/reflection/slang_bridge.cpp` configures
  `target_desc.format = SLANG_DXIL` but only queries reflection;
  `getEntryPointCode` / `getTargetCode` are *not* called and no IR
  blob is captured. Adding IR capture is one new code path inside
  the existing bridge — no second Slang pool, no second compile per
  source.
- **Cross-target by construction.** Some Phase 7 rules (the four
  ray-tracing rules + `maybereorderthread-without-payload-shrink`)
  reason about DirectX-specific intrinsics (`MaybeReorderThread`,
  `HitObject`, DXR `TraceRay` payload structs) that are first-class
  in DXIL but only surface as `OpExtInst` calls in SPIR-V. Other
  rules (precision/packing, register pressure, redundant samples)
  work fine over either IR. Pick a backend that covers DirectX
  cleanly and document SPIR-V as a follow-up.
- **Stable spans via debug-info round-tripping.** DXIL carries
  LLVM-bitcode debug locations when Slang is invoked with
  `-g`/`-line-directive-mode source-map`; SPIR-V carries
  `OpLine` + `OpString`. The bridge requests source-mapped
  output and the engine projects each IR instruction's source
  location back to a `(SourceId, ByteSpan)` via the `SourceManager`.
  When debug info is missing or unrecoverable for an instruction,
  the engine anchors to the entry-point's declaration span (already
  carried in `EntryPointInfo` per ADR 0012). Never invent a
  synthetic span.
- **`std::expected<IrInfo, Diagnostic>` at the API boundary.**
  Slang codegen failures (`getEntryPointCode` returns a non-zero
  `SlangResult`) surface as a single warn-severity `clippy::ir`
  diagnostic anchored to the entry-point's declaration span when
  available. IR-parse failures (DXIL container truncation, SPIR-V
  validation failure) surface the same way. AST/reflection/CFG
  rules continue to fire on the source.
- **Lazy invocation, keyed by `(SourceId, target_profile)`.**
  Unlike the AST CFG (per-`SourceId`), the IR view is target-
  dependent: SM 6.6 DXIL and SM 6.9 DXIL are different IR. The
  cache key matches ADR 0012's reflection cache key — the same
  tuple, the same `LintOptions::target_profile`, the same per-run
  cache lifetime.
- **Liveness and register-pressure ship together but separately
  from rule packs.** Three rules (`live-state-across-traceray`,
  `maybereorderthread-without-payload-shrink`,
  `groupshared-when-registers-suffice`) need per-instruction
  liveness; four rules (`vgpr-pressure-warning`,
  `scratch-from-dynamic-indexing`,
  `groupshared-when-registers-suffice`,
  `buffer-load-width-vs-cache-line`) need a register-pressure
  estimate. Both are shared utilities, not rules. They land in 7b.
- **Best-effort precision; rules ship at warn severity.** A
  Phase 7 rule that fires on a heuristic VGPR estimate or a
  liveness pass that didn't see partial-spill recovery is *advice*,
  not a hard correctness claim. Severity is bounded to *warn* until
  v0.8+ refines the estimator. The same convention as ADR 0013's
  uniformity oracle.

## Considered Options

### Option A — DXIL only (chosen for v0.7)

Capture the DXIL blob from the existing Slang `SLANG_DXIL` target,
parse it via a vendored DXIL reader, and run all 15 rules over the
resulting IR view. Defer SPIR-V to a follow-up ADR.

DXIL parser choice within Option A:
- **A1 — link DXC's `DxilContainer` reader (chosen).** DXC is
  MIT/LLVM-with-exception licensed (Apache-2.0 compatible per ADR
  0006). It is the canonical parser; vendor maintains the format.
  Cost: a hefty external dep (DXC pulls in LLVM headers; CMake
  integration via `find_package(DirectXShaderCompiler)` or
  submodule). Mitigated by the same 3-tier per-user prebuilt cache
  pattern ADR 0005 already uses for Slang.
- **A2 — roll our own LLVM-bitcode reader.** Smaller binary, no
  DXC dep. Cost: ~3000 LOC of LLVM-bitcode parsing logic that we
  would own forever. Rejected: too much surface area for a
  research-grade phase.

Trade-offs:
- Good: lowest-friction path. Slang already emits DXIL; no second
  compile path; `slang_bridge.cpp` change is one extra
  `getEntryPointCode` call inside the existing reflect path.
- Good: covers all 15 Phase 7 rules including the DirectX-only
  intrinsics (`MaybeReorderThread`, `HitObject`, DXR
  `TraceRay`).
- Bad: Vulkan / SPIR-V users see no IR-level diagnostics until
  the SPIR-V follow-up ships. Mitigated by clear documentation
  ("Phase 7 is DXIL-only at v0.7"); the rule pack works for the
  D3D12 audience that drove ADR 0010 / SM 6.9 demand.
- Bad: Microsoft-flavour DXIL is the *Microsoft* dialect of LLVM
  bitcode. AMD/NVIDIA/Intel drivers consume it but do not produce
  alternative DXIL. Acceptable for a linter — the linter
  diagnoses what Slang produces, which is exactly DXC-flavour DXIL.

**Chosen for v0.7.**

### Option B — SPIR-V only

Switch the bridge to emit SPIR-V (`SLANG_SPIRV`), parse with
spirv-tools (Apache-2.0, Khronos-stewarded), run all rules over the
SPIR-V module. DXIL-specific intrinsics (`MaybeReorderThread`,
`HitObject`) become opaque `OpExtInst` calls.

- Good: clean Apache-2.0 license; spirv-tools is a small,
  well-stewarded dep; cross-platform from day one.
- Good: spirv-opt provides existing liveness analysis we could
  reuse (avoiding ~150 LOC of dataflow).
- Bad: 5 of 15 rules (the four ray-tracing rules + the SM 6.9
  rule) reason about DirectX-specific intrinsics that SPIR-V does
  not surface as first-class instructions. The rule bodies become
  significantly more brittle (matching `OpExtInst` numeric IDs +
  DXC-flavour extended-instruction-set names that drift across
  Slang versions). Two of those five rules are the cross-vendor
  flagship Phase 7 demands — `live-state-across-traceray` and
  `oversized-ray-payload` — and shipping them on a brittle
  `OpExtInst` matcher is a release-quality regression vs DXIL.
- Bad: rebases the existing reflection bridge from `SLANG_DXIL`
  onto `SLANG_SPIRV`, which is a real change to the Phase 3
  contract (ADR 0012 §"Slang reflection API entry points
  referenced") even though reflection consumers don't read the
  target format.

**Rejected for v0.7.** Re-considered for v0.8 as Option C below.

### Option C — Both (DXIL + SPIR-V parallel engines)

Run the bridge twice per source per profile: once for DXIL, once
for SPIR-V. Parallel IR engines, parallel parser deps. Each rule
declares which IR it consumes; rules that are cross-target run on
both and union diagnostics; rules that are DirectX-only (the
ray-tracing pack + SM 6.9 rule) run on DXIL only.

- Good: covers DirectX flagship rules at full fidelity AND gives
  Vulkan users IR-level coverage for the cross-target rules.
- Bad: doubles Slang compile cost per source per profile (two
  `getEntryPointCode` paths). Mitigated by the per-`(SourceId,
  target_profile, ir_format)` cache, but the wall-clock hit on the
  first lint run per file is real.
- Bad: doubles the parser dep surface (DXC + spirv-tools both
  required). Mitigated by gating each behind its own CMake option
  (`HLSL_CLIPPY_ENABLE_DXIL=ON`, `HLSL_CLIPPY_ENABLE_SPIRV=OFF`),
  but the v0.7 default still pulls DXC.
- Bad: too big for one ADR. Doubling the engine surface in the
  *introductory* IR phase is exactly the over-design ADR 0013
  warned against (its §"Option C — Hybrid" was rejected for the
  same reason).

**Defer to v0.8.** Option A (DXIL only) is the *minimum* viable
Phase 7 infrastructure. A SPIR-V engine is worth its own ADR once
the DXIL rules have shipped and surfaced real demand from the
Vulkan side of the user base.

## Decision Outcome

**Option A — DXIL-only IR engine for v0.7, SPIR-V deferred to a
follow-up ADR.** The proposed architecture, broken into the seven
concrete API additions that constitute it.

### 1. New public header `core/include/hlsl_clippy/ir.hpp` (opaque types only)

No `<slang.h>` include. No `<dxil_*.h>` include. No `<spirv-tools/*.h>`
include. Pure value types. Sketch (illustrative; the implementation
PR designs the C++ class signatures in detail per the ADR's "What
NOT to do" guard):

```cpp
namespace hlsl_clippy {

enum class IrOpcode : std::uint16_t {
    Unknown,
    // Coarse-grained opcode tags rules switch on. The bridge maps
    // DXIL/LLVM opcodes onto this enum; rules never see raw DXIL.
    Load, Store,
    Sample, SampleLevel, SampleGrad, SampleCmp,
    TraceRay, MaybeReorderThread, HitObjectInvoke,
    MeshSetOutputCounts, MeshOutputVertex, MeshOutputPrimitive,
    AluFloat, AluInt, Bitcast, F32ToF16, F16ToF32,
    Pack8888, Unpack8888,
    AllocaLocalArray, DynamicGep,
    Phi, Branch, Switch, Return,
    // ... extended in implementation PRs as rules need finer tags
};

class IrFunctionId {  // opaque handle, cheap to copy
public:
    constexpr IrFunctionId() noexcept = default;
    [[nodiscard]] constexpr std::uint32_t value() const noexcept;
private:
    friend class IrEngine;
    explicit constexpr IrFunctionId(std::uint32_t v) noexcept;
    std::uint32_t value_ = 0;
};

class IrBasicBlockId { /* same shape as IrFunctionId */ };
class IrInstructionId { /* same shape */ };

struct IrInstruction {
    IrInstructionId id{};
    IrOpcode opcode = IrOpcode::Unknown;
    Span span{};                          // best-effort source anchor
    std::vector<IrInstructionId> operands;
    std::optional<std::uint32_t> result_bit_width;  // 16/32/64 etc
};

struct IrBasicBlock {
    IrBasicBlockId id{};
    std::vector<IrInstructionId> instructions;
    std::vector<IrBasicBlockId> successors;
};

struct IrFunction {
    IrFunctionId id{};
    std::string entry_point_name;
    std::string stage;                    // "compute" / "raygeneration" / ...
    std::vector<IrBasicBlock> blocks;
    Span declaration_span{};
};

struct IrInfo {
    std::vector<IrFunction> functions;
    std::string target_profile;           // e.g. "sm_6_6"

    [[nodiscard]] const IrFunction* find_function_by_name(
        std::string_view name) const noexcept;
    [[nodiscard]] const IrInstruction* find_instruction(
        IrInstructionId) const noexcept;
};

}  // namespace hlsl_clippy
```

The implementation PR (sub-phase 7a) designs the field surface in
detail. This ADR commits only to: opaque IDs, source-anchored
spans, target-profile-keyed `IrInfo`, no DXIL/SPIR-V types in the
public header. `LivenessInfo` and `RegisterPressureEstimate` are
private utilities (per §6 below), not public types.

### 2. Extend `Stage` enum in `core/include/hlsl_clippy/rule.hpp`

```cpp
enum class Stage : std::uint8_t {
    Ast,           // Phase 0/1/2 (default)
    Reflection,    // Phase 3 — ADR 0012
    ControlFlow,   // Phase 4 — ADR 0013
    Ir,            // Phase 7 — this ADR
};
```

The default `Rule::stage()` continues to return `Stage::Ast`, so all
existing Phase 0/1/2/3/4 rules keep their behaviour with zero source
change. The `// future: Ir (Phase 7)` comment in the existing
`rule.hpp` becomes the actual enumerator.

### 3. New rule entrypoint `Rule::on_ir`

Add a fifth virtual alongside `on_node`, `on_tree`, `on_reflection`,
`on_cfg`:

```cpp
virtual void on_ir(const AstTree& tree,
                   const IrInfo& ir,
                   RuleContext& ctx);
```

Rules with `stage() == Stage::Ir` override this. IR rules retain
access to the `AstTree` because diagnostic anchoring sometimes
prefers the source declaration over the IR's debug-info span (e.g.
`oversized-ray-payload` anchors to the payload `struct` declaration
in source, not the `OpStore` instruction in IR). The orchestrator
never calls `on_ir` for non-IR rules and never calls AST / reflection
/ CFG hooks for `Stage::Ir` rules unless they explicitly opt in by
overriding multiple hooks.

### 4. Internal `IrEngine` (lives in `core/src/ir/`)

New private module:

```
core/src/ir/
    engine.hpp          // public-to-core API; returns IrInfo
    engine.cpp          // orchestration + lint-run cache
    dxil_bridge.hpp     // narrow bridge surface (opaque to rules)
    dxil_bridge.cpp     // ONLY TU that includes <DxilContainer.h>
    debug_info.hpp/cpp  // DXIL debug-info → (SourceId, ByteSpan)
```

Responsibilities of `IrEngine`:

- **Reuse the Slang `ISession` pool from ADR 0012.** The reflection
  bridge already owns the pool; the IR engine borrows it via a
  narrow accessor. Today `slang_bridge.cpp` calls `link()` and then
  `getLayout(0)` for reflection; the IR path additionally calls
  `getEntryPointCode(entry_index, target_index, &code_blob,
  &diag_blob)` per entry point and captures the resulting DXIL
  blob. One Slang compile per `(SourceId, target_profile)` services
  both reflection and IR.
- **Parse the DXIL blob via DXC's `DxilContainer` reader.**
  `dxil_bridge.cpp` is the **only** TU under `core/` that includes
  DXC headers. CI grep is extended in 7a to forbid `<dxil_*.h>` and
  `<DxilContainer.h>` outside `core/src/ir/`.
- **Project DXIL debug-info onto `(SourceId, ByteSpan)`.** DXIL
  carries LLVM-bitcode debug locations referencing the source path
  + line/col; `debug_info.cpp` resolves these against the
  `SourceManager` and produces stable byte-spans. When debug info
  is missing for an instruction, the engine anchors to the
  enclosing entry-point's declaration span (already carried by
  `EntryPointInfo`). Never invent synthetic spans.
- **Per-lint-run cache** keyed by `(SourceId, target_profile)`.
  `std::flat_map` per CLAUDE.md "C++23 idioms to use". Cache
  lifetime is one `lint()` invocation. Same key as ADR 0012's
  reflection cache; in practice the two caches share the underlying
  Slang `ISession` acquire/release.
- **`std::expected<IrInfo, Diagnostic>` at the API boundary.**
  `getEntryPointCode` failures, container-parse failures, and
  unsupported-DXIL-version failures all surface as one
  warn-severity `clippy::ir` diagnostic per source per profile,
  anchored to the first entry-point's declaration span. AST /
  reflection / CFG rules continue to fire.

### 5. Extended `LintOptions` (additive on top of ADRs 0012 + 0013)

```cpp
struct LintOptions {
    // --- existing fields (ADR 0012 + 0013) ---
    std::optional<std::string> target_profile;
    bool          enable_reflection      = true;
    std::uint32_t reflection_pool_size   = 4;
    bool          enable_control_flow    = true;
    std::uint32_t cfg_inlining_depth     = 3;

    // --- new fields (this ADR) ---

    /// When false, the IR stage is skipped entirely even if IR-stage
    /// rules are enabled. Useful for CI runs that want to isolate
    /// Phase 7 cost, or for downstream consumers built with
    /// HLSL_CLIPPY_ENABLE_IR=OFF where the engine simply isn't
    /// linked in.
    bool          enable_ir              = true;

    /// Per-instruction live-value count above which
    /// vgpr-pressure-warning fires. Default 64 (RDNA wave32 *2).
    /// Per-arch refinement is a follow-up.
    std::uint32_t vgpr_pressure_threshold = 64;
};
```

The pipeline inspects every enabled rule's `stage()` once at the
start of `lint()`. If no rule has `stage() == Stage::Ir`, the
`IrEngine` is never constructed and `enable_ir` /
`vgpr_pressure_threshold` are silently ignored. This keeps AST /
reflection / CFG-only runs at zero IR cost.

### 6. Shared utilities for Phase 7 rules

Per ADR 0012 / 0013's pattern, three shared-utility headers under
`core/src/rules/util/`:

- **`ir_helpers.hpp/cpp`** — common IR queries:
  `find_entry_point(ir, name)`, `is_ray_stage(stage)`,
  `is_mesh_stage(stage)`, `enclosing_block(ir, instr_id)`,
  `successors_transitive(ir, block_id)`,
  `instruction_kind_in_block(ir, block_id, opcode)`. Used by
  every Phase 7 rule pack.
- **`liveness.hpp/cpp`** — backward dataflow over `IrFunction`.
  Computes per-instruction live-in / live-out as a bit-set keyed
  by `IrInstructionId`. ~150 LOC of standard SSA-style backward
  fixed-point iteration. Used by `live-state-across-traceray`,
  `maybereorderthread-without-payload-shrink`, and
  `groupshared-when-registers-suffice`. Liveness is *not*
  exposed in the public `ir.hpp` — it is a private utility behind
  the rule-pack boundary.
- **`register_pressure.hpp/cpp`** — heuristic per-block VGPR /
  register count. v0.7 implementation: count live SSA values × an
  estimated bit-width (`result_bit_width / 32` rounded up; pairs
  for f64/i64). Per-target tables for VGPR register file widths
  (RDNA 32-lane wave, NVIDIA Turing/Ada warp, Intel Xe-HPG) live
  inside this TU and are *not* exposed publicly. False-positive
  prone but no external dep, no per-arch driver wrangling. Used
  by `vgpr-pressure-warning`, `scratch-from-dynamic-indexing`,
  `groupshared-when-registers-suffice`,
  `buffer-load-width-vs-cache-line`. Refinement (linkable RGA
  integration, accurate per-arch counts) is explicitly v0.8+.

These are private headers; rules `#include "rules/util/..."` and
never see DXIL types directly.

### 7. CMake gating + CI grep

- New CMake option `HLSL_CLIPPY_ENABLE_IR` (default `ON` for the
  v0.7 release build). When `OFF`, `core/src/ir/` is excluded from
  the build, `Stage::Ir` still exists in the enum, but the
  orchestrator returns the warn-severity `clippy::ir` diagnostic
  ("IR stage compiled out — rebuild with -DHLSL_CLIPPY_ENABLE_IR=ON
  to enable") for any source where an IR-stage rule is enabled.
  Downstream consumers who only want AST + reflection + CFG
  surface (the LSP server's hot path, e.g.) build with
  `HLSL_CLIPPY_ENABLE_IR=OFF` and pay zero DXC dep cost.
- New CI grep clause: `<dxil_*.h>`, `<DxilContainer.h>`, and any
  `dxc/` include must not appear under `core/include/` or
  `core/src/rules/`. The clause sits next to the existing
  `<slang.h>` clauses from ADRs 0001 + 0012.
- DXC is integrated via the same 3-tier cache pattern ADR 0005
  uses for Slang: `cmake/UseDxc.cmake` looks for a per-user
  prebuilt at `~/.cache/hlsl-clippy/dxc-<sha>/`, falls back to a
  pinned submodule build with sccache, and pins the version via
  `cmake/DxcVersion.cmake`.

## Implementation sub-phases

Mirrors ADR 0013's "infra PR + shared-utilities PR + parallel
category packs" pattern. **Do not parallelise sub-phases 7a and
7b** — they share design surface and serialising them avoids
merge-time drift. Sub-phase 7c is the parallel-pack dispatch.

### Sub-phase 7a — infrastructure PR (sequential, must land first)

Single PR, single agent. Lands:

- `core/include/hlsl_clippy/ir.hpp` (opaque types per §1).
- New private module `core/src/ir/` (engine, dxil_bridge,
  debug_info per §4). `dxil_bridge.cpp` is the only TU that
  includes DXC headers.
- Modify `core/src/reflection/slang_bridge.cpp` to additionally
  call `getEntryPointCode` and stash the DXIL blob alongside the
  reflection result. Reflection-only callers see no behaviour
  change; IR-engine callers now have a blob to parse.
- Extend `Stage` enum (§2) and `Rule::on_ir` virtual (§3).
- Extend `LintOptions` with the two IR fields (§5). Existing
  fields preserved.
- Wire `IrEngine` into the orchestrator: stage-dispatch logic
  inspects rules' `stage()`, lazily constructs the engine, dispatches
  `on_ir` against the cached `IrInfo`.
- New CMake option `HLSL_CLIPPY_ENABLE_IR` (§7). New CI grep clause
  for DXC headers (§7).
- `cmake/UseDxc.cmake` + `cmake/DxcVersion.cmake` per §7.
- New unit-test TU `tests/unit/test_ir.cpp` with smoke tests:
  - IR for a one-entry-point compute shader has exactly one
    `IrFunction`.
  - IR debug-info for a `Sample` instruction projects to the
    expected source byte-span.
  - IR-engine returns warn-severity `clippy::ir` diagnostic when
    `getEntryPointCode` fails (synthetic broken shader).
  - `HLSL_CLIPPY_ENABLE_IR=OFF` build excludes `core/src/ir/`
    from the link; orchestrator returns the documented
    "compiled out" diagnostic.

Effort: **~2.5 dev weeks.** DXC integration + DXIL parser +
debug-info projection + engine wiring is the largest single
infrastructure PR after Phase 4. No rules added — just the engine.

### Sub-phase 7b — shared-utilities PR (sequential, lands second)

Single PR, single agent. Lands:

- `core/src/rules/util/ir_helpers.hpp/cpp` per §6.
- `core/src/rules/util/liveness.hpp/cpp` per §6 (~150 LOC backward
  dataflow + unit tests).
- `core/src/rules/util/register_pressure.hpp/cpp` per §6 (heuristic
  per-block VGPR estimate + per-arch tables + unit tests).
- Doc-page seeding under `docs/rules/` for the 14 Phase 7 rules +
  the SM 6.9 `maybereorderthread-without-payload-shrink` (status
  `Pre-v0`).

Effort: **~5 dev days.** No rules added.

### Sub-phase 7c — parallel rule-pack dispatch

After 7a + 7b land, dispatch up to **4 parallel agents**:

- **Pack A — register-pressure (5 rules):** `vgpr-pressure-warning`,
  `scratch-from-dynamic-indexing`, `redundant-texture-sample`,
  `groupshared-when-registers-suffice`,
  `buffer-load-width-vs-cache-line`.
- **Pack B — precision/packing (3 rules):**
  `min16float-opportunity`, `unpack-then-repack`,
  `manual-f32tof16`.
- **Pack C — ray tracing (5 rules):** `oversized-ray-payload`,
  `missing-accept-first-hit`, `recursion-depth-not-declared`,
  `live-state-across-traceray`,
  `maybereorderthread-without-payload-shrink` (the SM 6.9 rule
  shares Pack C's liveness machinery per ADR 0010 §"Risks").
- **Pack D — mesh / amplification (2 rules):**
  `meshlet-vertex-count-bad`, `output-count-overrun`.

Each pack agent works in its own worktree under
`.claude/worktrees/phase7-pack-{A..D}/`, branched from `main`,
merged back via `--no-ff` per CLAUDE.md "Worktree-from-main; close
the merge loop". All 4 packs share only the 7a + 7b surface — no
cross-pack design coupling.

This ADR does **not** select which 4 rules to ship first — that is
the v0.7 release plan's responsibility. The pack split above is the
*infrastructure* boundary, not a release-content commitment.

Phase 7 closes when all 4 packs have merged and CI is green on
`main`. Phase 7 has no successor phase under the current roadmap;
"closing Phase 7" means all 14 (+1) rules are shipped at warn
severity with documented heuristics.

## Consequences

- AST / reflection / CFG-only lint runs (Phase 0/1/2/3/4 rule
  selection) stay fast — the `IrEngine` never constructs and the
  DXIL parse cost is never paid. Verified by a unit test that runs
  the default Phase 4 rule pack and asserts the IR engine factory
  was never called.
- Phase 7 work depends on Phase 3 / ADR 0012 (the reused Slang
  `ISession` pool) but does NOT depend on Phase 4 / ADR 0013.
  IR has its own basic-block structure distinct from the AST CFG.
  Where Phase 7 rules want CFG-style facts, they consume the IR's
  successor edges directly via `IrBasicBlock::successors`.
- New external dep: DXC (MIT/LLVM-with-exception, Apache-2.0
  compatible per ADR 0006). The 3-tier cache pattern from ADR 0005
  absorbs the build-time cost; first-time builds without the
  cache pay a real DXC-from-source cost on the very first IR-engine
  build. Mitigated by `HLSL_CLIPPY_ENABLE_IR=OFF` for AST-only
  downstream consumers.
- Public API gains exactly one new header (`ir.hpp`), one new
  `Stage` value, one new `Rule` virtual, and two new `LintOptions`
  fields. All additive — no break for current API consumers. ADRs
  0012 and 0013 are unchanged; this ADR is strictly additive on top
  of their `Stage` extensions.
- Phase 7's register-pressure estimator is a heuristic at v0.7.
  False positives are bounded by warn severity + the
  `vgpr_pressure_threshold` knob + inline suppression (per ADR
  0008). v0.8+ refines the estimator (linkable AMD RGA, accurate
  per-arch counts) under a follow-up ADR; that work is **not**
  blocking for v0.7's Phase 7 release.
- macOS / Metal target: Slang can also emit Metal via
  `SLANG_METAL_LIB`. **Phase 7 does not need it.** Metal IR
  consumption is explicitly out of scope and is not an oversight —
  the four Phase 7 rule packs all target either DirectX-flagship
  features (DXR, mesh / amplification, SM 6.9 SER) or hardware
  patterns (RDNA / Turing / Ada / Xe-HPG VGPR / LDS occupancy)
  whose Metal equivalents would need a separate research pass.
  Note this explicitly so reviewers don't mistake the omission
  for an oversight.
- Vulkan / SPIR-V users see no IR-level diagnostics at v0.7.
  Documented as such; SPIR-V is a v0.8+ follow-up ADR (per
  Option C above, deferred).

## Risks & mitigations

- **Risk: DXC ABI churn breaks the DXIL parser across version
  bumps.** Mitigation: `cmake/DxcVersion.cmake` pins
  `HLSL_CLIPPY_DXC_VERSION` and the per-user prebuilt cache is
  keyed by that string, so a submodule SHA bump invalidates stale
  cache entries automatically. CI runs against the pinned version.
  Bump deliberately in a focused PR with `dxil_bridge.cpp` updated
  in lockstep.

- **Risk: DXIL debug-info is incomplete or absent for some
  instructions.** Slang may not propagate `!dbg` metadata for every
  intrinsic lowering, especially for ray-tracing and mesh-shader
  intrinsics where the lowering inserts internal helper blocks.
  Mitigation: when an instruction has no debug-info anchor, the
  engine falls back to the enclosing entry-point's declaration
  span (already in `EntryPointInfo`). Diagnostics still produce a
  span; the span is just coarser. Authors can track precise
  source-to-IR mapping gaps as upstream Slang issues; the linter
  does not block on them.

- **Risk: register-pressure estimator false-positive rate is high
  enough to be noisy.** Heuristic estimates that count live SSA
  values × bit-width *will* over-count compared to a real
  register-allocator. Mitigation: rules ship at warn severity; the
  `vgpr_pressure_threshold` knob defaults conservatively (64,
  ~2× a wave's worth of full-width VGPRs); inline suppression per
  ADR 0008 lets authors silence false positives per call site;
  v0.8+ refines via a follow-up ADR (linkable RGA / per-arch
  counts).

- **Risk: liveness analysis cost on large compute shaders.** A
  5,000-line shader with deep loop nesting may have hundreds of
  basic blocks per function. Mitigation: per-source cache (one
  IR build + one liveness pass per source per lint run); the
  per-`(SourceId, target_profile)` cache amortises across all 8
  rules that consume liveness or register-pressure facts. Hard
  opt-out via `LintOptions::enable_ir = false`.

- **Risk: SM 6.9 rule (`maybereorderthread-without-payload-shrink`)
  needs DXIL extensions Slang's released versions don't yet
  emit.** SM 6.9 is forward-looking per ADR 0010; the rule may be
  un-testable until Slang's SM 6.9 codegen lands in a pinned
  version. Mitigation: Pack C ships the four DXR rules first; the
  SM 6.9 rule lands in a follow-up patch tied to the Slang version
  pin that emits SM 6.9 DXIL. ADR 0010 already gates SM 6.9 rules
  behind `HLSL_CLIPPY_SLANG_VERSION` ≥ the SM 6.9 release.

## More Information

- **Cross-references**:
  - ADR 0001 (Slang choice — IR engine reuses the existing Slang
    `ISession` pool).
  - ADR 0002 (parser — `(SourceId, ByteSpan)` is the only span
    representation that crosses the rule boundary; debug-info
    projection lands instructions onto that representation).
  - ADR 0004 (C++23 baseline — `std::expected`, `std::flat_map`
    used throughout the engine).
  - ADR 0005 (CI/CD — DXC absorbed into the same 3-tier cache
    pattern Slang uses).
  - ADR 0006 (license — DXC MIT/LLVM-with-exception is
    Apache-2.0 compatible).
  - ADR 0007 §Phase 7 (the original 13 IR-level rules this ADR
    unblocks).
  - ADR 0010 §"Risks" (`maybereorderthread-without-payload-shrink`
    shares this ADR's liveness machinery; lands in Pack C).
  - ADR 0011 §Phase 7 (the 2 LOCKED rules
    `groupshared-when-registers-suffice` and
    `buffer-load-width-vs-cache-line` this ADR unblocks).
  - ADR 0012 (Phase 3 reflection infrastructure — sibling
    infrastructure ADR; this ADR's `IrEngine` reuses its
    `ISession` pool).
  - ADR 0013 (Phase 4 control-flow infrastructure — sibling
    infrastructure ADR; this ADR mirrors its sub-phase pattern;
    IR has its own block structure independent of the AST CFG).
- **DXIL parser API surface** (in `dxil_bridge.cpp` only, never in
  public headers): `hlsl::DxilContainerHeader`,
  `hlsl::DxilModule`, `llvm::Function`, `llvm::BasicBlock`,
  `llvm::Instruction`, `llvm::DebugLoc`. Pinned to the
  `HLSL_CLIPPY_DXC_VERSION` API surface.
- **Algorithmic references** (used in `core/src/rules/util/` only,
  never leaked through public headers):
  - Standard backward-liveness fixed-point iteration over the
    SSA join-lattice.
  - Per-target VGPR width tables sourced from public IHV
    documentation (AMD RDNA 1/2/3 ISA reference, NVIDIA Turing
    / Ampere / Ada whitepapers, Intel Xe-HPG architecture
    documents).

## Open question

Should the IR engine support multiple IR formats per `(SourceId,
target_profile)` tuple, or one format per cache entry?

This ADR commits to **DXIL only** at v0.7. The cache key is
`(SourceId, target_profile)`; there is no `ir_format` axis. A
follow-up SPIR-V ADR (deferred per Option C above) would extend the
cache to `(SourceId, target_profile, ir_format)` and add a new
`LintOptions::ir_format` field. That extension is straightforward
mechanically, but the rule API question is harder: should `on_ir`
be invoked once per format (rule iterates), or should the rule
declare which format it consumes and the orchestrator route only?
Pack C's ray-tracing rules need DXIL-specific instructions and
would prefer the route-only model; Pack B's precision-packing rules
work fine on either format and would prefer iterate-once.

This ADR defers the question. The proposed `LintOptions` is
DXIL-only; per-format dispatch (probably via a future
`Rule::ir_formats() -> std::span<const IrFormat>` virtual) is a
tracked follow-up worth its own ADR before any Phase 7 rule
depends on a non-DXIL format.
