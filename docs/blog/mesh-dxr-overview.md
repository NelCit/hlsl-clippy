---
title: "Mesh shader output topology, DXR payload pressure, and the silent failure modes between them"
date: 2026-05-01
author: NelCit
category: mesh-dxr
tags: [hlsl, shaders, performance, mesh, dxr, ray-tracing, amplification]
license: CC-BY-4.0
---

A mesh shader compiles. The PSO creates. The frame renders. Most of the
time, every meshlet appears on screen. Every now and again — when a
particular camera angle happens to make a particular cull predicate flip,
or when an amplification thread group sees zero surviving meshlets — a
patch of geometry vanishes for one frame. Or doesn't. Or the GPU TDRs.
The author's amplification shader ends with this:

```hlsl
[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID) {
    Payload p = build_payload(gtid);
    if (p.meshletCount == 0) {
        return;                       // skip the empty dispatch
    }
    DispatchMesh(p.meshletCount, 1, 1, p);
}
```

The intent is reasonable: skip the dispatch on empty work. The actual
behaviour is undefined. The amplification stage's contract requires
exactly one `DispatchMesh` call per thread group, on every CFG path that
reaches a return. Skipping it on the empty-work path strands the
geometry front-end on RDNA 2/3 (which deadlocks waiting for the launch
handshake), causes silent meshlet drops on Turing / Ada (the mesh phase
never wakes up for that group), and TDRs on Xe-HPG. What `dxc` accepts
cleanly the hardware treats as a contract violation, and the visual
symptom is something between "intermittent missing geometry" and
"device removed".

This is the family of bugs `hlsl-clippy`'s mesh and DXR rules target.
The mesh-pipeline rules are well-developed; five of them ship in v0.5
and cover the largest mesh-shader footguns: the per-group thread cap,
the per-group output declaration cap, the amplification payload cap,
the mesh-output overrun pattern, and the "DispatchMesh missing on some
path" pattern shown above. The DXR side of this category is younger
— only one rule ships in v0.5 (the `RAY_FLAG_CULL_NON_OPAQUE`
omission), and the rest of the DXR rule pack is queued for v0.6 and
v0.7. This post leans hard on the mesh side and treats DXR as a
smaller second half.

## What mesh shaders actually do

Before the mesh pipeline, the per-frame geometry path on D3D12 was a
fixed sequence of programmable stages: vertex shader, optional hull /
domain shaders for tessellation, an optional geometry shader for
amplification, then primitive setup and rasterisation. Every triangle
flowed through that pipe. The amplification axis was hostile: spawning
extra triangles inside a geometry shader was notoriously slow, and
arbitrary topology changes mid-pipeline were either expensive or
unrepresentable.

The mesh pipeline collapses all of that into two compute-style stages:
amplification (optional) and mesh (mandatory). The amplification stage
is a workgroup that runs once per draw, decides how many child mesh
groups to launch, and packages a payload describing the per-child
work. The mesh stage runs once per child group and emits a small
bounded array of vertices and primitive indices directly into the
rasteriser's input. There is no intervening cache; the output buffer
declared by the mesh shader is the rasteriser's input buffer.

That collapse buys two things. First, geometry amplification becomes
cheap and SIMT-friendly: the AS workgroup runs as compute, makes a
purely numeric decision about how much work to spawn, and hands off
via `DispatchMesh`. Second, per-meshlet culling becomes the natural
shape of the work: a meshlet either wholly survives the AS-stage
culling test or it never gets dispatched.

The cost is a new contract surface the old fixed-function pipeline did
not impose. Mesh and amplification shaders run inside a budget shaped
by the IHVs' mesh-output staging hardware, and that budget is tighter
than it looks. On AMD RDNA 2/3, the mesh-output buffer is LDS-resident,
sized at PSO-creation time from the declared `out vertices` / `out
indices` arrays and trimmed at launch by `SetMeshOutputCounts`. On
NVIDIA Turing and Ada Lovelace, the per-meshlet output region is sized
for at most 256 vertices and 256 primitives multiplied by the per-
vertex output stride. On Intel Xe-HPG, the equivalent staging area
enforces the same caps as part of the D3D12 conformance contract.
Stepping outside any of those caps is not a perf footgun; it is a hard
PSO-creation failure or, worse, an undefined-behaviour failure at
launch.

## The hard PSO caps: thread count, output dimensions, payload size

Three mesh rules catch the same family of bug at three different
contract surfaces. All three are PSO-creation failures: if you ship
the shader, `D3D12CreateGraphicsPipelineState` returns `E_INVALIDARG`
and the engine never gets to render anything. Catching them at lint
time replaces a confusing runtime error message with a precise source
location.

[`mesh-numthreads-over-128`](/rules/mesh-numthreads-over-128) targets
the per-group thread cap. The mesh-pipeline specification caps both
amplification and mesh entry points at 128 threads per group: any
`[numthreads(X, Y, Z)]` whose product exceeds 128 fails PSO creation.
The reason is that all three IHVs reserve a per-launch scoreboard
quantum sized to the cap, and the rasteriser-handoff hardware on RDNA
and the MNG (mesh next-gen) handshake on NVIDIA both expect the
launch unit to be at most 128 threads. Most production mesh shaders
end up at `[numthreads(64, 1, 1)]` or `[numthreads(128, 1, 1)]` —
sized to map cleanly onto wave64 (RDNA) or two wave32s (NVIDIA), with
the latter being the sweet spot for occupancy on cards that hit the
LDS budget early.

[`mesh-output-decl-exceeds-256`](/rules/mesh-output-decl-exceeds-256)
targets the output declaration cap. `out vertices` and `out indices`
arrays must each be at most 256 elements; either one exceeding the
cap is a PSO-creation failure. The number 256 is not arbitrary — it is
chosen so the per-meshlet output region fits in the LDS budget on
RDNA 2 (the most LDS-constrained of the three modern IHVs) when
combined with the typical per-vertex output stride. Writing the
declaration as `verts[64]` / `tris[124]` (the meshoptimizer
convention) or `verts[128]` / `tris[128]` (the NVIDIA-recommended
starting point) leaves enough LDS headroom for high wave occupancy
and is the sizing every cross-vendor production codebase converges
on.

[`as-payload-over-16k`](/rules/as-payload-over-16k) targets the
amplification-shader payload cap. The struct passed from `DispatchMesh`
to the child mesh shaders lives in a per-AS-group on-chip staging
region, and the cap on that region is 16 KB. NVIDIA stages the payload
through a portion of the SM's shared memory; AMD stages it through the
mesh-pipeline LDS slot; Intel stages it through the per-AS scoreboard
region. The cap is the contract every IHV ships, sized to fit
comfortably in the most constrained of the three (RDNA 2 LDS).
Production AS payloads typically stay well under 1 KB — a meshlet-id
array, an LOD selector, and a culling decision are usually all that
is needed. When a payload approaches 16 KB, the right refactor is to
move the bulk data into a `StructuredBuffer` indexed by
`SV_DispatchThreadID` inside the mesh shader, and keep only the
indexing metadata in the payload itself. The doc page walks through
that refactor in detail.

These three rules are, individually, simple constant-fold-and-compare
checks. They are valuable because the failure mode is "the engine
crashes at PSO-creation time with `E_INVALIDARG`" — a notoriously
unhelpful error in a large engine where dozens of PSOs are being
created at startup and the failed one is identified by a hash, not a
filename.

## The contract bugs: dispatch missing, primcount overrun

Two more mesh rules target subtler failure modes that compile cleanly
and crash at runtime.

[`dispatchmesh-not-called`](/rules/dispatchmesh-not-called) is the
contract bug from the opening of this post. The amplification stage
must call `DispatchMesh(x, y, z, payload)` exactly once per thread
group on every reachable CFG path. The most common origin of the bug
is an early return guarded by a per-launch condition (`if
(meshletCount == 0) return;`), but `switch` cases that diverge on
per-launch state and loops whose exit path bypasses the dispatch are
the same family. The hardware behaviour on a missing dispatch is
explicitly undefined: RDNA 2/3 deadlocks the geometry front-end, Ada
silently drops the meshlet (the symptom is missing geometry on a
fraction of frames), and Xe-HPG TDRs. The fix is uniform: call
`DispatchMesh(0, 1, 1, p)` (or whatever the runtime treats as a
no-op-equivalent dispatch) on the empty-work path and let the runtime
handle the empty case. The HLSL specification does not allow more
than one `DispatchMesh` per thread group either, so the rule's CFG
analysis enumerates every return-reaching path and flags the first
one missing the call.

[`primcount-overrun-in-conditional-cf`](/rules/primcount-overrun-in-conditional-cf)
is the more insidious sibling. The mesh shader calls
`SetMeshOutputCounts(maxVerts, maxPrims)` once at the top, then issues
primitive-index writes inside conditional control flow whose join
could push the live primitive count above `maxPrims` on at least one
path. The rule fires when CFG analysis can prove a path exists where
`primIndex >= maxPrims` reaches a primitive write. The hardware on
every IHV allocates output storage for exactly the declared count;
writing past the declared count is undefined behaviour, and the
hardware may silently drop the over-count primitive, may overwrite a
neighbouring meshlet's output (corrupting another lane group's
geometry), or may surface a TDR. The reproducibility varies by IHV
and by driver version, which makes the bug a classic intermittent
crash. The cleanest fix is to wave-fold the survivor count first
(`WaveActiveCountBits` of the per-lane survival predicate), pass that
exact count to `SetMeshOutputCounts`, then write into a per-lane
prefix-sum slot — a refactor the doc page walks through. Less
invasive fixes (relax the count to the worst case, or gate writes on
`i < maxPrims`) are also valid and cheaper; the rule names the path
and lets the author choose.

These two rules require CFG analysis (Phase 4 in the project's
roadmap), which is more involved than the constant-fold checks of the
PSO-cap rules. The payoff is that the bug class they catch is exactly
the one that survives `dxc`'s validator, the IHV's debug runtime, and
PIX / RGP / Nsight captures, and ships into production. They are the
mesh-pipeline analogues of the divergent-barrier and divergent-discard
rules in the control-flow category.

## DXR — where the rule pack is younger

The DXR side of this category is much thinner in v0.5 than the mesh
side, and the project owes you an honest accounting of that. One DXR
rule ships:
[`missing-ray-flag-cull-non-opaque`](/rules/missing-ray-flag-cull-non-opaque).
The remainder — payload sizing, recursion-depth declaration,
conditional-`TraceRay`, accept-first-hit shadow rays, oversized
payload, inline-vs-pipeline ray tracing — are doc-stubbed but not yet
shipped. They land progressively in v0.6 and v0.7 as the control-flow
infrastructure (Phase 4) and the rest of the reflection-aware rule
pack (Phase 3) finish landing. The pack is genuinely useful even in
this thin form because the one shipped rule targets the single largest
portable RT-core footgun, but it is not yet the full pack the docs
catalogue suggests.

[`missing-ray-flag-cull-non-opaque`](/rules/missing-ray-flag-cull-non-opaque)
catches `TraceRay(...)` and `RayQuery::TraceRayInline(...)` calls
whose ray-flag argument does not include `RAY_FLAG_CULL_NON_OPAQUE`
in a context where the bound any-hit shader is empty (or where
reflection shows no any-hit shader is bound to the relevant hit
groups). The reason this is a portable footgun is structural to how
RT cores work on every modern IHV. DXR traversal on NVIDIA Turing
and Ada Lovelace RT cores, AMD RDNA 2/3 Ray Accelerators, and Intel
Xe-HPG RTU all split BVH leaf processing into two paths: the *opaque
path*, where the leaf primitive is accepted directly by the
traversal hardware, and the *non-opaque path*, where the hardware
suspends traversal, returns to the SIMT engine, runs the any-hit
shader, and resumes. The opaque path stays inside the RT hardware
end-to-end. The non-opaque path costs a full shader invocation per
leaf hit, including the wave reformation and the trip back through
the scheduler. NVIDIA's Ada whitepaper measures the per-non-opaque-
hit cost at roughly 30-60 ALU cycles of overhead on top of the any-
hit shader's own work — even when the any-hit body is empty.

`RAY_FLAG_CULL_NON_OPAQUE` instructs the traversal hardware to skip
non-opaque geometry entirely. When the application has no real any-
hit logic — common in shadow rays and primary-visibility rays where
alpha-test is not in scope — the flag turns every potentially-non-
opaque BVH visit into a no-op inside the RT cores. The reported
speedups on shadow-ray-heavy production workloads (Cyberpunk 2077,
Portal RTX) range from 5% to 15% of total RT time on the affected
passes. The fix is a single bit OR'd into the ray-flag constant. The
linter cannot prove safety unconditionally — when a hit group
genuinely needs alpha-tested geometry, the flag must not be set —
so the rule fires only when reflection or AST scanning shows the
any-hit body is dead, which is the safe direction.

A note on DXR ray-payload sizing, since the topic naturally pairs
with the AS-payload discussion above: payload size in bytes feeds
directly into LDS pressure across the recursion stack on every IHV,
because each recursion level reserves staging room for one payload.
A 64-byte payload that could have been 32 halves your in-flight ray
count. NVIDIA Ada and AMD RDNA 3 both pay the cost as reduced wave
occupancy on raygen / closest-hit; Xe-HPG pays it as reduced
traversal-pipeline parallelism. The rule that catches oversized
payloads is doc-stubbed but not yet implemented; when it ships it
will be a structural sizing check analogous to
[`as-payload-over-16k`](/rules/as-payload-over-16k).

## Run it on your shaders today

The five mesh rules and the one DXR rule in v0.5 catch the common
mesh-pipeline failure modes: PSO-creation failures (thread / output /
payload caps), undefined-behaviour failures (`DispatchMesh` missing,
primcount overrun), and the largest portable RT-core footgun
(missing `RAY_FLAG_CULL_NON_OPAQUE`). The mesh side is mature
enough to be useful as a CI gate today. The DXR side is honestly
thin and grows over the next two releases.

```sh
hlsl-clippy lint --format=github-annotations shaders/mesh
```

Per-rule severity, suppression, and `--fix` rewrites work the same
way they do for every other rule in the linter. The mesh rules
default to `error` severity (every one of them flags either a hard
PSO-creation failure or undefined behaviour) and the DXR rule
defaults to `warn` (the missing flag is a perf footgun, not a
correctness bug). Per-line suppression follows the project-wide
convention:

```hlsl
DispatchMesh(0, 1, 1, p); // hlsl-clippy: allow(dispatchmesh-not-called)
```

Use it in CI to catch the failures before they hit the device. If
you encounter a mesh-pipeline or DXR pattern that should be a lint
rule and is not yet covered, open an issue — the DXR side
particularly welcomes corpus contributions from production
ray-tracing codebases, since the pack's coverage gaps in v0.5 are
the next priority for the v0.6 release.

---

`hlsl-clippy` is an open-source HLSL linter. Rules, issues, and
discussion live at
[github.com/NelCit/hlsl-clippy](https://github.com/NelCit/hlsl-clippy).
If you have encountered a shader pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
