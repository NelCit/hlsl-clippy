---
id: unused-cbuffer-field
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# unused-cbuffer-field

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A field declared inside a `cbuffer`, `ConstantBuffer<T>`, or user-defined struct used as a constant buffer template type, that is never read in any shader entry point or function reachable from an entry point in the same compilation unit. The rule uses Slang's reflection API and reachability analysis: it enumerates all cbuffer fields by name and byte offset, then checks each field name against the set of identifiers referenced in the shader's AST. A field that appears in the declaration but never in an expression is flagged. See `tests/fixtures/phase3/bindings.hlsl`, line 36 (`float4 c` in `UnusedFields`) and `tests/fixtures/phase3/bindings_extra.hlsl`, line 24 (`uint DebugChannel` in `DebugCB`).

## Why it matters on a GPU

An unused cbuffer field wastes constant-cache capacity. The GPU loads the entire cbuffer or the portion covering the declared fields into the constant-data cache (AMD K-cache, NVIDIA L1 constant cache) as a contiguous block. Fields that are never read by the shader still occupy cache lines in that block, evicting data from other cbuffers or from other fields of the same cbuffer that are actively used.

For the AMD RDNA K-cache (16 KB per CU, shared across all in-flight waves), each unused field that pushes the cbuffer's live region across a cache-line boundary increases the number of cache lines the cbuffer occupies, reducing the effective capacity available to other uniform data on the same CU. On NVIDIA hardware, the constant cache is 8 KB per SM; an oversized cbuffer with unused fields is more likely to exceed the cache and require re-fetches from L2 DRAM on subsequent dispatches using the same cbuffer layout.

Beyond cache pressure, unused fields indicate a maintenance hazard: the CPU side fills the field (burns bandwidth and CPU time computing or uploading the value), but the GPU side ignores it. Over time, unused fields accumulate in "kitchen sink" cbuffers as shader variants diverge, leading to cbuffers that are both oversized and underutilised. Removing unused fields also reduces the constant buffer's declared size, potentially enabling the `cbuffer-fits-rootconstants` rule to fire and suggest migration to root constants.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 33-38
// HIT(unused-cbuffer-field): `c` declared but never referenced in entry_main.
struct UnusedFields {
    float4 a;
    float4 b;
    float4 c;  // HIT(unused-cbuffer-field): never read
};
ConstantBuffer<UnusedFields> Unused : register(b4);

// From tests/fixtures/phase3/bindings_extra.hlsl, lines 22-25
// HIT(unused-cbuffer-field): DebugChannel declared but never referenced.
cbuffer DebugCB {
    float4 DebugTint;
    uint   DebugChannel;   // HIT(unused-cbuffer-field): never read below
};
```

### Good

```hlsl
// Remove the unused field entirely.
struct UsedFields {
    float4 a;
    float4 b;
    // float4 c removed — no longer declared, no longer uploaded
};
ConstantBuffer<UsedFields> Used : register(b4);

cbuffer DebugCB {
    float4 DebugTint;
    // DebugChannel removed
};
```

## Options

none

## Fix availability

**suggestion** — Deleting an unused field is a one-line edit in the HLSL source, but the rewrite is *not* safe to auto-apply because removing a field shifts the byte offsets of every subsequent field in the cbuffer and shrinks the cbuffer's declared size. CPU-side struct mirrors that `memcpy` into the binding will silently write into the wrong offsets after the change, and any other shader that aliases the same cbuffer slot will read garbage. The lint surfaces the dead field; the deletion needs to land alongside the matching CPU-side cleanup, which is outside this rule's scope. If you want the rule to emit a TextEdit you can review-and-apply by hand, that is tracked as a follow-up.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — alignment gaps that may grow after field removal
- Related rule: [oversized-cbuffer](oversized-cbuffer.md) — cbuffer exceeds the default 4 KB threshold
- Related rule: [cbuffer-fits-rootconstants](cbuffer-fits-rootconstants.md) — removing unused fields may shrink the cbuffer to root-constant size
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/unused-cbuffer-field.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
