// Fixture (b) per ADR 0020 sub-phase A: Slang-language-flavoured content
// that uses HLSL-equivalent constructs (cbuffer, RWStructuredBuffer) and
// would parse cleanly through Slang's HLSL frontend on a `.hlsl` file.
// On `.slang`, Slang routes through its native frontend; reflection-stage
// rules still observe the resource layout.
//
// Expected output: exactly one `clippy::language-skip-ast` Note plus any
// Reflection-stage diagnostics that fire on the cbuffer / buffer
// declarations below. AST-stage diagnostics MUST NOT fire (no parse).

// SHOULD-NOT-HIT(saturate-redundant) -- AST stage skipped on .slang

cbuffer SceneConstants : register(b0) {
    float4 view_position;
    float  time;
    float  delta_time;
    float2 viewport_size;
}

RWStructuredBuffer<float4> g_scene_buffer : register(u0);

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    const float t = saturate(saturate(view_position.x));  // SHOULD-NOT-HIT(redundant-saturate)
    g_scene_buffer[dispatch_id.x] = float4(t, t, t, 1.0);
}
