// Phase 7 — work graphs (SM 6.8) rules. Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

// SM 6.8 work graph types and attributes.

// --- nodeid-implicit-mismatch ---

struct DrawRecord {
    uint meshIndex;
    uint materialIndex;
    float4x4 transform;
};

// Downstream node is named "ProcessDraw" in C++ root signature, but the
// NodeOutput declaration below has no explicit [NodeId(...)] attribute.
// HIT(nodeid-implicit-mismatch): implicit node ID derived from the output struct
// name "DrawRecord" likely disagrees with the downstream node's registered name.
[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeDispatchGrid(1, 1, 1)]
void DispatchNode(
        DispatchNodeInputRecord<DrawRecord> input,
        [MaxRecords(64)] NodeOutput<DrawRecord> ProcessDraw)
{
    DrawRecord rec = input.Get();
    ThreadNodeOutputRecords<DrawRecord> out = ProcessDraw.GetThreadNodeOutputRecords(1);
    out.Get() = rec;
    OutputComplete(out);
}

// SHOULD-NOT-HIT(nodeid-implicit-mismatch): explicit [NodeId] on the output
// matches the downstream node name unambiguously.
[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeDispatchGrid(1, 1, 1)]
void DispatchNodeSafe(
        DispatchNodeInputRecord<DrawRecord> input,
        [MaxRecords(64)] [NodeId("ProcessDraw", 0)] NodeOutput<DrawRecord> outNode)
{
    DrawRecord rec = input.Get();
    ThreadNodeOutputRecords<DrawRecord> out = outNode.GetThreadNodeOutputRecords(1);
    out.Get() = rec;
    OutputComplete(out);
}

// --- outputcomplete-missing ---

struct SortKey {
    uint key;
    uint value;
};

// HIT(outputcomplete-missing): OutputComplete is never called on the early-return
// path — every CFG path that calls GetThreadNodeOutputRecords must pair it with
// OutputComplete before the shader exits.
[Shader("node")]
[NodeLaunch("thread")]
void SortNode(
        ThreadNodeInputRecord<SortKey> input,
        [MaxRecords(1)] NodeOutput<SortKey> Sorted)
{
    SortKey k = input.Get();
    if (k.key == 0xFFFFFFFFu) return;   // early exit — OutputComplete never called

    ThreadNodeOutputRecords<SortKey> out = Sorted.GetThreadNodeOutputRecords(1);
    out.Get() = k;
    OutputComplete(out);
}

// SHOULD-NOT-HIT(outputcomplete-missing): every path calls OutputComplete.
[Shader("node")]
[NodeLaunch("thread")]
void SortNodeSafe(
        ThreadNodeInputRecord<SortKey> input,
        [MaxRecords(1)] NodeOutput<SortKey> Sorted)
{
    SortKey k = input.Get();
    ThreadNodeOutputRecords<SortKey> out = Sorted.GetThreadNodeOutputRecords(1);
    if (k.key == 0xFFFFFFFFu) {
        SortKey zero = { 0u, 0u };
        out.Get() = zero;
    } else {
        out.Get() = k;
    }
    OutputComplete(out);
}

// --- quad-or-derivative-in-thread-launch-node ---

struct PixelTask {
    float2 uv;
    uint   layer;
};

Texture2D    TexInput : register(t0);
SamplerState LinearSS : register(s0);

// HIT(quad-or-derivative-in-thread-launch-node): QuadReadAcrossX and an implicit-
// gradient Sample inside a thread-launch node — no quad topology; result is undefined.
[Shader("node")]
[NodeLaunch("thread")]
void PixelProcessNode(ThreadNodeInputRecord<PixelTask> input)
{
    PixelTask task = input.Get();
    float2 uv = task.uv;

    // HIT(quad-or-derivative-in-thread-launch-node): ddx() has no meaning in
    // a thread-launch node — there is no 2x2 pixel quad.
    float ddx_uv = ddx(uv.x);

    // HIT(quad-or-derivative-in-thread-launch-node): implicit-derivative Sample
    // inside thread-launch node — undefined gradient.
    float4 col = TexInput.Sample(LinearSS, uv);

    // HIT(quad-or-derivative-in-thread-launch-node): QuadReadAcrossX requires
    // quad topology that does not exist in thread-launch nodes.
    float neighbourU = QuadReadAcrossX(uv.x);

    // suppress use
    (void)(ddx_uv + col.r + neighbourU);
}
