---
id: quad-or-derivative-in-thread-launch-node
category: work-graphs
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# quad-or-derivative-in-thread-launch-node

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Work-graph nodes declared with `[NodeLaunch("thread")]` (thread-launch nodes, where each input record produces a single thread invocation rather than a thread group) that contain quad-scoped intrinsics (`QuadAny`, `QuadAll`, `QuadReadAcrossX`, `QuadReadAcrossY`, `QuadReadAcrossDiagonal`, `QuadReadLaneAt`), explicit derivatives (`ddx`, `ddy`, `ddx_fine`, `ddx_coarse`, `ddy_fine`, `ddy_coarse`), or implicit-derivative texture sampling (`Texture2D::Sample`, `SampleBias`, `SampleCmp`). All of these require quad-coupled execution that thread-launch nodes do not provide.

## Why it matters on a GPU

Work graphs offer three node launch modes: broadcasting (one thread group per record, like compute), coalescing (multiple records folded into one thread group), and thread (one thread per record). The thread-launch mode is the most flexible for fine-grained dispatch but it explicitly does not guarantee the quad-coupled execution that pixel-shader-style derivatives and quad intrinsics require. There is no rasterizer feeding 2x2 quads of co-located lanes; there is no helper-lane mechanism; there is no guarantee that any group of four lanes in a wave correspond to anything spatially or logically related. A `QuadReadAcrossX` issued in a thread-launch node reads from a "quad neighbour" that the runtime has no obligation to make meaningful.

On AMD RDNA 3 with work-graph support, thread-launch nodes pack records densely into waves for SIMD utilisation; whatever ends up adjacent in the wave is whatever fit there. A `QuadReadAcrossX` reads from the lane two slots away in the wave (RDNA's quad mapping), but that lane's record is unrelated. On NVIDIA Ada / Blackwell work-graph implementations, the warp-scoped quad intrinsics behave the same way: the lane fetched is the lane's neighbour in the warp packing, not a logical 2D neighbour. Implicit-derivative `Sample` calls compute `ddx`/`ddy` of the texture coordinate against arbitrary-record neighbours, producing meaningless gradients and consequently arbitrary mip selection — the texture fetches "succeed" but read from random mip levels with random spatial coherency.

The HLSL specification for SM 6.8 work graphs (sec. 8.5 of the work-graphs HLSL spec) labels these intrinsics as illegal in thread-launch nodes; the validator should emit an error, but in practice early DXC versions warn and proceed, and inline-spelled functions called from a thread-launch node with derivatives buried inside escape detection. The fix is structural: either change the node launch mode to broadcasting (gives up some dispatch granularity but reinstates quad execution where derivatives matter), use `SampleLevel` with explicit LOD to avoid the derivative requirement, or move the derivative-dependent work to a downstream broadcasting node that consumes records produced by the thread-launch node.

## Examples

### Bad

```hlsl
struct WorkRecord { float2 uv; };

Texture2D    Albedo : register(t0);
SamplerState Samp   : register(s0);

[Shader("node")]
[NodeLaunch("thread")]
[NumThreads(1, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void node_thread_launch_with_sample(
    DispatchNodeInputRecord<WorkRecord> input)
{
    WorkRecord r = input.Get();
    // Implicit-derivative Sample inside a thread-launch node — no quad
    // coupling, derivatives are arbitrary, mip selection is random.
    float4 col = Albedo.Sample(Samp, r.uv);
    // QuadAny in a thread-launch node — neighbours are unrelated records.
    bool any_lit = QuadAny(col.r > 0.5);
    Out[input.Get().uv] = any_lit ? col : 0.xxxx;
}
```

### Good

```hlsl
[Shader("node")]
[NodeLaunch("thread")]
[NumThreads(1, 1, 1)]
[NodeMaxDispatchGrid(1024, 1, 1)]
void node_thread_launch_explicit_lod(
    DispatchNodeInputRecord<WorkRecord> input)
{
    WorkRecord r = input.Get();
    // Explicit LOD avoids derivatives entirely. No quad coupling required.
    float4 col = Albedo.SampleLevel(Samp, r.uv, 0);
    Out[r.uv] = col;
    // No QuadAny — if a wave-level reduction is needed, use WaveActiveAnyTrue
    // (which is allowed in thread-launch nodes, with the caveat that the
    // wave participants are arbitrary records, not spatial neighbours).
}
```

## Options

none

## Fix availability

**none** — The fix requires changing either the node's launch mode (a structural change to the work graph) or the texture sampling mode (`Sample` to `SampleLevel`/`SampleGrad`). Both choices have semantic implications that an automated rewrite cannot resolve. The diagnostic identifies the offending intrinsic and the enclosing thread-launch-node entry point.

## See also

- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — work-graph node output completion
- Related rule: [sample-in-loop-implicit-grad](sample-in-loop-implicit-grad.md) — derivative-coupled sampling in non-quad CF
- Microsoft DirectX docs: Work Graphs — Thread-Launch Nodes (HLSL spec sec. 8.5)
- Companion blog post: [work-graphs overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/quad-or-derivative-in-thread-launch-node.md)

*© 2026 NelCit, CC-BY-4.0.*
