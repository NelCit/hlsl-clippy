// Phase 7 — register pressure / IR-level rules. Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.
//
// NOTE ON IR-LEVEL RULES: vgpr-pressure-warning and redundant-texture-sample are
// IR-level rules that operate on compiled DXIL / SPIR-V, not the HLSL AST.
// The HIT annotations below mark the SOURCE PATTERN that correlates with the
// IR rule firing; the actual diagnostic will be emitted after compilation.
// `scratch-from-dynamic-indexing` is partially syntactic and does fire on AST.

Texture2D    TexA : register(t0);
Texture2D    TexB : register(t1);
Texture2D    TexC : register(t2);
SamplerState SS   : register(s0);

cbuffer PressureCB {
    float2 UV0;
    float2 UV1;
    float  Blend;
    float  Exposure;
    uint   DynIdx;   // per-draw uniform but unknown at compile time
};

// --- vgpr-pressure-warning (IR-level) ---

// HIT(vgpr-pressure-warning): this function keeps many float4 values live
// simultaneously — a static live-range estimate will flag it as high VGPR pressure.
// Actual threshold is stage-specific (PS/CS differ). IR-level rule; annotation
// marks the source pattern correlating with the IR diagnostic.
float4 ps_high_pressure(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // 12 live float4 values concurrently — each 16 bytes → 192 bytes of VGPR.
    float4 a0 = TexA.Sample(SS, uv + float2(0.00, 0.00));
    float4 a1 = TexA.Sample(SS, uv + float2(0.01, 0.00));
    float4 a2 = TexA.Sample(SS, uv + float2(0.02, 0.00));
    float4 a3 = TexA.Sample(SS, uv + float2(0.03, 0.00));
    float4 b0 = TexB.Sample(SS, uv + float2(0.00, 0.01));
    float4 b1 = TexB.Sample(SS, uv + float2(0.01, 0.01));
    float4 b2 = TexB.Sample(SS, uv + float2(0.02, 0.01));
    float4 b3 = TexB.Sample(SS, uv + float2(0.03, 0.01));
    float4 c0 = TexC.Sample(SS, uv + float2(0.00, 0.02));
    float4 c1 = TexC.Sample(SS, uv + float2(0.01, 0.02));
    float4 c2 = TexC.Sample(SS, uv + float2(0.02, 0.02));
    float4 c3 = TexC.Sample(SS, uv + float2(0.03, 0.02));
    return (a0+a1+a2+a3 + b0+b1+b2+b3 + c0+c1+c2+c3) * (Exposure / 12.0);
}

// SHOULD-NOT-HIT(vgpr-pressure-warning): sequential accumulation; live range short.
float4 ps_low_pressure(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    acc += TexA.SampleLevel(SS, uv + float2(0.00, 0.00), 0);
    acc += TexA.SampleLevel(SS, uv + float2(0.01, 0.00), 0);
    acc += TexB.SampleLevel(SS, uv + float2(0.00, 0.01), 0);
    acc += TexC.SampleLevel(SS, uv + float2(0.00, 0.02), 0);
    return acc * (Exposure / 4.0);
}

// --- scratch-from-dynamic-indexing ---

// HIT(scratch-from-dynamic-indexing): dynamically indexing a local array forces
// the array into scratch memory (register file spill) on most GPUs because the
// index is unknown at compile time.
float4 ps_dynamic_array_index(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 lut[8];
    lut[0] = float4(1, 0, 0, 1);
    lut[1] = float4(0, 1, 0, 1);
    lut[2] = float4(0, 0, 1, 1);
    lut[3] = float4(1, 1, 0, 1);
    lut[4] = float4(0, 1, 1, 1);
    lut[5] = float4(1, 0, 1, 1);
    lut[6] = float4(0.5, 0.5, 0.5, 1);
    lut[7] = float4(1, 1, 1, 1);
    // DynIdx is unknown at compile time → forces scratch / register-file indexing.
    return lut[DynIdx & 7u] * Exposure;
}

// SHOULD-NOT-HIT(scratch-from-dynamic-indexing): index is a compile-time constant.
float4 ps_static_array_index(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 lut[4] = { float4(1,0,0,1), float4(0,1,0,1), float4(0,0,1,1), float4(1,1,1,1) };
    return lut[2] * Exposure;   // constant index — stays in registers
}

// --- redundant-texture-sample (IR-level) ---

// HIT(redundant-texture-sample): TexA.Sample at the same UV sampled twice in
// the same basic block — the compiler may not CSE across the barrier; IR rule
// catches this after compilation when the DXIL shows duplicate sample ops.
// Annotation marks the source pattern; diagnostic is IR-level.
float4 ps_duplicate_sample(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 first  = TexA.Sample(SS, uv);     // sample #1
    float4 second = TexA.Sample(SS, uv);     // HIT(redundant-texture-sample): identical UV+tex
    return lerp(first, second, Blend) * Exposure;
}

// SHOULD-NOT-HIT(redundant-texture-sample): UVs differ — not the same sample.
float4 ps_two_different_samples(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 s0 = TexA.Sample(SS, UV0);
    float4 s1 = TexA.Sample(SS, UV1);
    return lerp(s0, s1, Blend) * Exposure;
}
