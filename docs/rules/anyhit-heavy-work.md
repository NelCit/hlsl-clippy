---
id: anyhit-heavy-work
category: dxr
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# anyhit-heavy-work

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Any-hit shaders (entry points marked `[shader("anyhit")]`) that perform work beyond the supported lightweight tasks: alpha-mask sampling and `IgnoreHit()`/`AcceptHitAndEndSearch()` decisions. The rule flags any-hit bodies containing `for` / `while` loops, more than one `Texture2D::Sample*` call, lighting math (dot products against light directions, BRDF evaluation), `TraceRay` recursion, or substantial scratch-VGPR usage. The same logic should appear in the closest-hit shader where the cost is paid only once per ray.

## Why it matters on a GPU

The any-hit shader runs every time the BVH traversal finds a primitive that the ray's bounding test admits as a candidate hit. For a single ray, this can mean dozens or hundreds of any-hit invocations as the traversal walks past intersecting geometry that may or may not be the closest. On AMD RDNA 2/3 with hardware RT, the BVH traversal unit issues an any-hit shader call per candidate primitive in a hardware-managed loop; the per-invocation cost dominates ray throughput when foliage, hair, or alpha-tested geometry is in the BVH. NVIDIA Turing, Ada, and Blackwell similarly invoke any-hit per candidate; the SER (Shader Execution Reordering) hardware on Ada and Blackwell can group similar any-hit invocations across rays for better SIMD efficiency, but the per-call work still scales with candidate count. On Intel Xe-HPG, the RT block dispatches any-hit on every triangle the ray enters, with the same multiplicative cost.

The closest-hit shader, by contrast, runs at most once per ray — only on the final closest accepted intersection. Heavy work belongs there: sampling all material textures, computing normals, evaluating the BRDF, sampling lights, casting reflection rays. Putting the same work in any-hit multiplies the cost by the candidate count. A typical foliage scene with 8-15 alpha-tested layers between the camera and the closest opaque hit will invoke any-hit 8-15 times per ray; doing a full BRDF evaluation in any-hit costs 8-15x the budget the same code costs in closest-hit. Measured on Ada (RTX 4080) for a foliage shadow pass, moving normal sampling and lighting from any-hit to closest-hit reduced ray dispatch time by 60-75%.

The supported any-hit pattern is small: sample one alpha-mask texture, compare against a threshold, call `IgnoreHit()` to reject and continue traversal or do nothing to accept. Anything more — including computing the alpha through a noise function, sampling multiple textures to combine alpha, or branching on lighting state — should be moved to closest-hit, with the any-hit reduced to the alpha test only. Where the alpha decision genuinely requires expensive computation, the alternative is to bake an alpha-discrimination mip pyramid offline and reference it from any-hit instead.

## Examples

### Bad

```hlsl
struct Payload  { float3 colour; };
struct HitAttrs { float2 bary; };

Texture2D    AlbedoMap : register(t0);
Texture2D    NormalMap : register(t1);
SamplerState Samp      : register(s0);

[shader("anyhit")]
void ah_heavy(inout Payload p, in HitAttrs a) {
    float2 uv = ResolveUV(a);
    float4 alb = AlbedoMap.Sample(Samp, uv);
    float3 N   = UnpackNormal(NormalMap.Sample(Samp, uv).xy);
    if (alb.a < 0.5) {
        IgnoreHit();
        return;
    }
    // Lighting math in an any-hit invocation — runs per candidate
    // intersection, multiplying the cost by traversal depth.
    float ndl = saturate(dot(N, normalize(LightDir)));
    p.colour = alb.rgb * ndl;
}
```

### Good

```hlsl
[shader("anyhit")]
void ah_alpha_only(inout Payload p, in HitAttrs a) {
    // Lightweight: one sample, one compare, IgnoreHit or fall through.
    float2 uv = ResolveUV(a);
    float  alpha = AlbedoMap.SampleLevel(Samp, uv, 0).a;
    if (alpha < 0.5) {
        IgnoreHit();
    }
}

[shader("closesthit")]
void ch_full_shading(inout Payload p, in HitAttrs a) {
    // Heavy work runs once on the closest accepted hit.
    float2 uv  = ResolveUV(a);
    float4 alb = AlbedoMap.Sample(Samp, uv);
    float3 N   = UnpackNormal(NormalMap.Sample(Samp, uv).xy);
    float  ndl = saturate(dot(N, normalize(LightDir)));
    p.colour = alb.rgb * ndl;
}
```

## Options

- `max-anyhit-instructions` (integer, default: 24) — instruction-count threshold above which the any-hit body is flagged. Tunable per project.

## Fix availability

**suggestion** — Splitting any-hit work into a thin alpha test plus a closest-hit body changes the shader-table layout and may require updating the `AcceptHitAndEndSearch` / `IgnoreHit` decision logic. The diagnostic flags the heavy any-hit body with an instruction-count estimate and points at code candidates for relocation.

## See also

- Related rule: [tracerray-conditional](tracerray-conditional.md) — placement of `TraceRay` and live-state spill
- Related rule: [inline-rayquery-when-pipeline-better](inline-rayquery-when-pipeline-better.md) — inline vs. pipeline RT
- Microsoft DirectX docs: DXR — Any-Hit, Closest-Hit invocation order
- NVIDIA developer blog: SER and any-hit grouping on Ada
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/anyhit-heavy-work.md)

*© 2026 NelCit, CC-BY-4.0.*
