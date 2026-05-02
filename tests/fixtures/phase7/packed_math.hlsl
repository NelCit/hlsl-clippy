// Phase 7 — packed math / fp16 rules (SM 6.4+ / SM 6.6+).
// Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

// SM 6.4 / 6.6 intrinsic prototypes (not natively declared in older SDKs;
// treated as opaque built-ins for fixture annotation purposes).

RWStructuredBuffer<uint> PackedOutput : register(u0);

cbuffer PackCB {
    float4 ColorIn;     // 32-bit float colour to be packed
    uint   PackedInput; // pre-packed RGBA8 value
    float  ScaleBias;
};

cbuffer MinMaxCB {
    // min16float loaded from a 32-bit cbuffer field — rule fires on reads below.
    float  FloatField;    // 32-bit cbuffer entry ...
};

// --- pack-then-unpack-roundtrip ---

// HIT(pack-then-unpack-roundtrip): unpack_u8u32 immediately followed by pack_u8
// HIT(unpack-then-repack): same pair detected by the Phase 7 rule (ADR 0017).
// with no modification — the pair is a dead round-trip; the original PackedInput
// is unchanged after both operations.
uint pack_unpack_roundtrip(uint packed) {
    uint4 unpacked = unpack_u8u32(packed);
    return pack_u8(unpacked);
}

// HIT(pack-then-unpack-roundtrip): f32tof16 followed by f16tof32 with no ALU
// HIT(unpack-then-repack): same pair detected by the Phase 7 rule (ADR 0017).
// between them — conversion pair is dead; use the original float.
float f16_roundtrip(float x) {
    uint h = f32tof16(x);
    return f16tof32(h);
}

// SHOULD-NOT-HIT(pack-then-unpack-roundtrip): intermediate modification breaks the
// SHOULD-NOT-HIT(unpack-then-repack): same — intermediate modification.
// round-trip — the unpacked channels are individually scaled before repacking.
uint pack_with_modify(uint packed) {
    uint4 channels = unpack_u8u32(packed);
    channels.r = channels.r >> 1u;   // half the red channel
    return pack_u8(channels);
}

// --- pack-clamp-on-prove-bounded ---

// HIT(pack-clamp-on-prove-bounded): ColorIn.rgba is already saturated (all values
// in [0,1]) so multiplied by 255.0 yields [0,255] — pack_clamp_u8 is redundant;
// use the truncating pack_u8 (one fewer clamp in the packing path).
uint pack_clamped_saturated(float4 col) {
    float4 s = saturate(col);               // provably in [0,1]
    uint4  u = (uint4)(s * 255.0 + 0.5);   // provably in [0,255]
    // HIT(pack-clamp-on-prove-bounded): clamp is provably no-op here.
    return pack_clamp_u8(u);
}

// SHOULD-NOT-HIT(pack-clamp-on-prove-bounded): input not proven bounded; clamp needed.
uint pack_unbounded(float4 col) {
    uint4 u = (uint4)(col * 255.0);
    return pack_clamp_u8(u);
}

// --- min16float-in-cbuffer-roundtrip ---

// HIT(min16float-in-cbuffer-roundtrip): FloatField is a 32-bit cbuffer float;
// casting to min16float re-pays a 32→16 conversion on every read. If the intent
// is fp16 precision, the cbuffer field itself should be fp16 (or explicitly
// use a half-precision buffer to avoid repeated conversion overhead).
min16float get_as_half() {
    return (min16float)FloatField;
}

// HIT(min16float-opportunity): chain widens a `half` value to 32-bit and then
// multiplies by a 16-bit-representable constant; the chain could stay at
// `min16float` and double packed-fp16 throughput on every IHV (ADR 0017).
float min16_opportunity_pattern(half h_in) {
    return (float)h_in * 0.5;
}

// HIT(manual-f32tof16): hand-rolled FP32 -> FP16 lowering matching the
// canonical `(asuint(x) >> 13) & 0x7FFF` form (ADR 0017).
uint manual_f32tof16_pattern(float x) {
    uint h = (asuint(x) >> 13) & 0x7FFF;
    return h;
}

// SHOULD-NOT-HIT(min16float-in-cbuffer-roundtrip): single explicit cast at use
// site is the documented work-around; rule targets the pattern where the cast
// is inside a hot loop that re-reads the same cbuffer field each iteration.
float use_as_float() {
    return FloatField * ScaleBias;
}

// --- dot4add-opportunity ---

// HIT(dot4add-opportunity): 4-tap integer dot product computed via shifts, masks,
// and adds — one dot4add_u8packed instruction replaces ~8 ALU ops.
uint manual_dot4_u8(uint a_packed, uint b_packed) {
    uint a0 = (a_packed      ) & 0xFFu;
    uint a1 = (a_packed >>  8) & 0xFFu;
    uint a2 = (a_packed >> 16) & 0xFFu;
    uint a3 = (a_packed >> 24) & 0xFFu;
    uint b0 = (b_packed      ) & 0xFFu;
    uint b1 = (b_packed >>  8) & 0xFFu;
    uint b2 = (b_packed >> 16) & 0xFFu;
    uint b3 = (b_packed >> 24) & 0xFFu;
    return a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3;
}

// SHOULD-NOT-HIT(dot4add-opportunity): using the SM 6.4 intrinsic — already optimal.
uint dot4add_correct(uint a, uint b, uint acc) {
    return dot4add_u8packed(a, b, acc);
}

// Entry point.
[numthreads(64, 1, 1)]
void cs_packed_math(uint3 dtid : SV_DispatchThreadID) {
    uint idx     = dtid.x;
    uint rt      = pack_unpack_roundtrip(PackedInput);
    uint clamped = pack_clamped_saturated(ColorIn);
    uint dot4    = manual_dot4_u8(PackedInput, rt);
    float frt    = f16_roundtrip(FloatField);
    min16float hf = get_as_half();
    PackedOutput[idx] = rt ^ clamped ^ dot4 ^ asuint(frt + (float)hf);
}
