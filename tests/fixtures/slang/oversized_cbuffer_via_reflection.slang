// Fixture (d) per ADR 0020 sub-phase A v1.3.1: a `.slang` source whose
// reflection result trips a Stage::Reflection rule (`oversized-cbuffer`).
// 512 float4 entries = 8 KB, comfortably over the 4 KB threshold the rule
// guards. Slang's `.slang` frontend ingests `ConstantBuffer<T>` with a
// large array cleanly; the bridge surfaces the layout through the same
// `CBufferLayout` shape that fires on HLSL.
//
// Expected output (v1.3.1+): exactly one `clippy::language-skip-ast`
// Note + at least one `oversized-cbuffer` Warning. Pre-v1.3.1 the
// reflection rule was silently quarantined and only the skip notice
// surfaced.
//
// HIT(oversized-cbuffer) -- Stage::Reflection fires on the 8 KB cbuffer.
// SHOULD-NOT-HIT(redundant-saturate) -- AST stage skipped on .slang.

struct BulkConstants {
    float4 entries[512];
};

ConstantBuffer<BulkConstants> bulk;

[shader("pixel")]
float4 ps_main() : SV_Target {
    return bulk.entries[0];
}
