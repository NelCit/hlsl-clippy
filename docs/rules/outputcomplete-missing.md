---
id: outputcomplete-missing
category: work-graphs
severity: error
applicability: none
since-version: v0.4.0
phase: 4
language_applicability: ["hlsl"]
---

# outputcomplete-missing

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Work-graph node entry points that obtain output records via `GetGroupNodeOutputRecords(...)`, `GetThreadNodeOutputRecords(...)`, or the analogous empty-record variants, but do not call `OutputComplete()` on every control-flow path that exits the node. The rule reports both the obvious case (a function that returns without ever calling `OutputComplete`) and the structural case (a node that calls `OutputComplete` inside one branch of an `if` but not on the other branch, or inside a loop body where some iterations may exit without committing).

## Why it matters on a GPU

Work graphs (D3D12 introduced in 2024, SM 6.8) are a producer/consumer dispatch model: each node entry point can request output records into a downstream node's input queue, fill those records, and then commit them via `OutputComplete()`. The hardware does not actually issue the downstream node's invocations until the commit happens. If a node obtains output records and exits without calling `OutputComplete()`, the downstream queue is left holding "in-flight" record slots that are neither consumed nor freed; the work-graph scheduler treats them as pending forever. On AMD RDNA 3 (the first hardware to ship work-graph support, via driver-managed scheduling), this manifests as a stalled graph: subsequent dispatches into the affected node hang waiting for queue capacity. On NVIDIA Ada and Blackwell with mesh-node and thread-launch-node support, the same behaviour applies; the node-launch scheduler reserves queue slots that never get released.

The hazard is harder to spot than a missing barrier because the runtime does not always crash. The driver may eventually time-slice through and detect the leak (typically via a TDR after a few seconds), or the workload may be small enough that the queue never fills and the bug stays latent. Production telemetry on early work-graph adopters has reported cases where the missing-`OutputComplete` bug shipped, manifested only on long sessions or after particular content was loaded, and required deep capture analysis to root-cause. The work-graph spec is explicit: every record obtained must be either explicitly cancelled or completed before the node invocation exits, on every CFG path.

The structural case — `OutputComplete` inside one branch but not another — is particularly insidious because it tests fine on inputs that always take the completing path. A node that requests output records and only calls `OutputComplete` when a condition holds will leak records whenever the condition is false. The fix is to either always call `OutputComplete` (passing zero-record outputs is a valid completion) or to lift the completion to the unconditional epilogue of the node body, mirroring the requirement that `SetMeshOutputCounts` must run unconditionally in mesh shaders.

## Examples

### Bad

```hlsl
struct DownstreamRecord { uint payload; };

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxDispatchGrid(64, 1, 1)]
void node_leak(
    uint                                                    gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<DownstreamRecord>           Down)
{
    GroupNodeOutputRecords<DownstreamRecord> recs = Down.GetGroupNodeOutputRecords(32);
    if (gtid < 16) {
        recs.Get(gtid).payload = gtid * 2u;
        recs.OutputComplete();   // Only the gtid<16 path commits.
    }
    // gtid >= 16 returns here without OutputComplete — the 32-record
    // allocation is leaked into the downstream queue forever.
}
```

### Good

```hlsl
[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(32, 1, 1)]
[NodeMaxDispatchGrid(64, 1, 1)]
void node_committed(
    uint                                                    gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<DownstreamRecord>           Down)
{
    GroupNodeOutputRecords<DownstreamRecord> recs = Down.GetGroupNodeOutputRecords(32);
    if (gtid < 32) {
        recs.Get(gtid).payload = (gtid < 16) ? gtid * 2u : 0u;
    }
    // Unconditional commit — every CFG path that reaches the node exit
    // releases the queue records.
    recs.OutputComplete();
}
```

## Options

none

## Fix availability

**none** — Reasoning about whether a missing `OutputComplete` is a bug or an intentional cancel-and-discard requires understanding the algorithm's intent. The diagnostic identifies the `Get*OutputRecords` call and the CFG path that lacks a corresponding `OutputComplete`.

## See also

- Related rule: [setmeshoutputcounts-in-divergent-cf](setmeshoutputcounts-in-divergent-cf.md) — analogous unconditional-call requirement in mesh shaders
- Related rule: [quad-or-derivative-in-thread-launch-node](quad-or-derivative-in-thread-launch-node.md) — work-graph thread-launch-node restrictions
- Microsoft DirectX docs: Work Graphs — `OutputComplete`, record lifetime
- Companion blog post: [work-graphs overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/outputcomplete-missing.md)

*© 2026 NelCit, CC-BY-4.0.*
