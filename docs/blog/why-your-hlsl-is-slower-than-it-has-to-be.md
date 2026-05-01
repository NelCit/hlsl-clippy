---
title: "Why your HLSL is slower than it has to be"
date: 2026-05-01
author: NelCit
category: launch
tags: [hlsl, shaders, performance, launch]
license: CC-BY-4.0
---

Every graphics engineer has, at some point, opened a renderer's shader folder
that has not been profiled in eighteen months and quietly winced. The hot
G-buffer pass has a `pow(roughness, 2.0)` in it. There is a `groupshared`
array indexed by `threadID.x * 32`, perfectly aligned to torch every bank in
the LDS on RDNA. A pixel shader branches on a value that is uniform across
the wave and then `discard`s on the false side, splitting the wave's helper
lanes from its real lanes for the rest of the function. The DXIL compiles
clean. RGA shows nothing scary on the AMD path. Nsight is happy on the
NVIDIA path. You ship the frame, and you eat 0.3 ms you did not have to.

The patterns that hurt are rarely visible at the syntactic level. They are
hardware-shaped: they show up on the ISA, in the dispatch occupancy table,
in the L1 hit rate, in the wave-divergence column of a profiler trace. They
are also, almost without exception, portable across vendors — the GPU memory
hierarchies on RDNA2/3, Turing, Ampere, Ada, and Xe-HPG have converged
enough in the last five years that what is bad on one is usually bad on the
others. That portability is the opening for a static linter that lives one
layer above the per-IHV analyzer.

`hlsl-clippy` is that linter. v0.5.0 ships 154 rules across fifteen
categories, every one of them rooted in a documented GPU mechanism, every
one with a companion blog post that explains why the pattern costs you
cycles. This post is the overview. The eight category posts that follow go
deep on specifics.

## What hlsl-clippy actually is

A static linter for HLSL. AST via tree-sitter-hlsl, compile and reflection
via Slang. Rules fire on three stages — pure-AST patterns (Phase 2),
reflection-aware rules that know your resource layout and target profile
(Phase 3), and control-flow / data-flow rules that reason about wave
uniformity, helper-lane semantics, and divergence (Phase 4). Every rule has
a doc page that explains the GPU mechanism in enough depth that you could
derive the rule yourself; many have a machine-applicable `--fix` rewrite.

What it is not: a profiler, a replacement for vendor tools, or a guarantee
that your shader will be faster after `--fix`. The point is to surface the
patterns that are provably suboptimal across IHVs at the ISA level, with
the GPU reasoning attached, so the warning doubles as a teaching tool.
DXC sometimes folds the patterns the linter flags. The rule's value lives
in the cases it does not — the cases where a `precise` qualifier suppressed
strength reduction, the SM target was older than DXC's aggressive folding
pass, or the subexpression was complex enough that the optimizer gave up.
We are honest about that in every doc page.

## Where the time goes

The rules fall into eight broad mechanism families. Each one corresponds to
a category-overview post that goes deeper than this one can. If you read
only the capsules below and stop, you should still come away with a working
mental model of which surfaces of HLSL hide hardware footguns.

### Transcendentals on every IHV

The full-rate VALU lane and the quarter-rate SFU/transcendental lane have
been a 4:1 throughput ratio on RDNA2, RDNA3, Turing, Ampere, and Ada
without meaningful exception. `pow(x, 2.0)` lowers to `exp2(2 * log2(x))`
when the compiler does not fold it; `pow(x, 5.0)` for Schlick Fresnel
almost never folds; `1.0 / sqrt(x)` should be `rsqrt(x)`; `sin(x)` near
small angles can be a Taylor expansion if the calling shader's tolerance
allows it. Roughly thirty rules in the math category target the
transcendental surface alone, and another dozen catch redundant
saturate / clamp01 patterns that the optimizer sometimes leaves behind.
[More in the math category overview](/blog/math-overview).

### LDS bank conflicts and groupshared layout

`groupshared` memory on every modern GPU is split across banks — 32 banks
on RDNA, 32 banks on NVIDIA SMs since Maxwell, 16 banks on Xe-HPG. A
write pattern of `lds[threadID.x * 32]` puts every lane on the same bank
and serializes the access for 32 cycles instead of 1. A struct-of-arrays
layout often beats array-of-structs for tile-local caches; padding a
`float3` to `float4` to avoid stride-3 conflicts is sometimes the right
answer and sometimes a waste of LDS budget that hurts occupancy. The
workgroup category has rules for stride detection, alignment hints,
groupshared-typed misuse, and `[numthreads]` shapes that are hostile to
wave packing. [More in the workgroup category overview](/blog/workgroup-overview).

### Helper-lane and divergence semantics

In a pixel shader, the four lanes of a 2x2 quad must execute in lockstep
through derivative-bearing operations (`ddx`, `ddy`, `Sample` without an
explicit gradient or LOD). When you `discard` a lane, it becomes a helper
lane for the rest of the wave's lifetime — still consuming a lane slot,
still required for derivatives, but contributing nothing to the
framebuffer. Branching on non-uniform conditions across a `Sample` call
silently breaks derivatives. The control-flow category catches the
patterns that break this contract — early `discard` placement, divergent
branches around derivative ops, `WaveActive*` calls in non-uniform control
flow — and explains the DXIL semantics that make each one a footgun.
[More in the control-flow category overview](/blog/control-flow-overview).

### Root signatures and binding shape

A root signature is not a free abstraction. Root constants land in scalar
registers (SGPRs on RDNA, uniform registers on NVIDIA) and are essentially
free to read. Root descriptors load through a one-step indirection. Tables
load through two. Putting a high-frequency CBV in a descriptor table and
a rarely-read one in root constants is a backwards layout that costs you
registers on the hot path and saves nothing on the cold path. The
bindings category has rules for root-signature shape, descriptor-table
sizing, push-constant overflow on consoles with tight root-signature
budgets, and `cbuffer` layouts that pack badly under `packoffset`.
[More in the bindings category overview](/blog/bindings-overview).

### Sampler hardware and texture access

Texture samplers are fixed-function on every GPU. Anisotropic filtering
costs more samples per call than bilinear and the cost scales with the
anisotropy ratio. Comparison samplers (`SampleCmp`) on shadow maps go
through a different path than the linear sampler and benefit from
hardware PCF when you use the right format. `Load` versus `Sample` on a
single-mip resource is a different ALU path, not a synonym. The texture
category catches sampler-format mismatches, redundant samples on the same
UV, `Sample` calls that should be `SampleLevel` to escape the derivative
requirement, and `Load` patterns that would benefit from the cache by
becoming `Sample`. [More in the texture category overview](/blog/texture-overview).

### Mesh shading, amplification, and DXR

Mesh shaders have hard per-group limits on output vertices and primitives
that vary by IHV — 256 vertices on RDNA2, 256 on Turing, higher on Ada,
and the dispatch occupancy depends on hitting them efficiently. DXR
ray-payload size in bytes feeds directly into LDS pressure across the
recursion stack; a payload that is 64 bytes when it could be 32 halves
your in-flight ray count. The mesh-and-DXR category has rules for
payload sizing, attribute packing, `[outputtopology]` correctness,
amplification group sizing, and the `MaybeReorderThread` / SER patterns
that are easy to get wrong. [More in the mesh and DXR category
overview](/blog/mesh-dxr-overview).

### Wave intrinsics and helper-lane traps

`WaveActiveSum`, `WaveReadLaneFirst`, `WaveActiveBallot`, and friends are
hardware-implemented on RDNA and NVIDIA Volta+ — they are fast when you
use them right and silently incorrect when you do not. Calling
`WaveReadLaneFirst` in non-uniform control flow returns a value from
whichever active lane the hardware picks, which is rarely what the author
meant. Helper lanes participate in `WaveActive*` ballots on some IHVs
and not others. The wave-helper-lane category has rules for
`WaveActive*` calls inside `if` branches that are not provably uniform,
quad-scope ops outside pixel shaders, and intrinsics that need an
explicit `[WaveSize]` annotation to behave consistently across IHVs.
[More in the wave-helper-lane category overview](/blog/wave-helper-lane-overview).

### SER, cooperative vectors, long vectors, OMM

Shader Model 6.9 added a substantial portable surface for raytracing and
ML inference: Shader Execution Reordering (SER) for coherence-aware
scheduling, cooperative vectors for matrix-multiply primitives that map
to NVIDIA Tensor Cores and AMD WMMA, long vectors that go beyond
`float4`, and Opacity Micromaps for cheaper alpha-tested traversal.
These are all preview-level surfaces and the patterns that hurt on them
are different from classical compute — payload coherence for
`MaybeReorderThread`, dimension alignment for cooperative-vector matmul,
LDS pressure from long-vector temporaries. The SM 6.9 category has rules
gated behind `[experimental]` config flags and aimed at engines adopting
the surface today. [More in the SER and cooperative-vector category
overview](/blog/ser-coop-vector-overview).

## What is NOT in the linter

The honest scope statement, since "what does this tool not do" is usually
more useful than the feature list:

- **DXC handles syntax.** `hlsl-clippy` does not duplicate the compiler's
  parser-level diagnostics. If your shader does not compile, fix that
  first.
- **RGA, Nsight, PIX, RenderDoc, and the platform-specific console tools
  have ground truth on their respective IHVs.** A real per-IHV ISA dump
  always beats a static linter. Use them when you have a frame to profile
  in front of you.
- **A profiler tells you which patterns are actually costing you frames.**
  `hlsl-clippy` flags what is provably suboptimal. The two answer
  different questions and you need both.

The portable middle layer — patterns that are bad on every IHV, with the
GPU reasoning attached, in a tool that runs in CI before the shader hits
a profiler — is the gap. That is the whole pitch.

## Run it on your shaders today

The build is from-source until v0.5.0 release artifacts publish (any
moment now, depending on when you read this). Prerequisites are a C++23
compiler — MSVC 19.44+, Clang 18+, or GCC 14+ — and CMake 3.20.

```sh
git clone --recurse-submodules https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy
cmake -B build && cmake --build build
./build/cli/hlsl-clippy lint shader.hlsl
./build/cli/hlsl-clippy lint --fix shader.hlsl
```

On Windows, `tools\dev-shell.ps1` enters the VS Dev Shell and adds the
Slang prebuilt cache to `PATH` in one step.

Configuration is a `.hlsl-clippy.toml` next to your shader tree:

```toml
[rules]
pow-const-squared    = "warn"
redundant-saturate   = "warn"
clamp01-to-saturate  = "off"

includes = ["shaders/**/*.hlsl"]
excludes = ["shaders/third_party/**"]
```

The CLI walks up from each shader's directory until it finds a config,
bounded by the nearest `.git/`. Per-rule severity, per-path overrides,
include / exclude globs, and a small set of `[experimental]` toggles for
preview surfaces all live in the same file.

For CI, drop the example workflow at
[`docs/ci/lint-hlsl-example.yml`](https://github.com/NelCit/hlsl-clippy/blob/main/docs/ci/lint-hlsl-example.yml)
into `.github/workflows/`. It downloads the latest release tarball, runs
the linter over your shader glob, and emits inline annotations on the PR
diff via `--format=github-annotations`. When `$GITHUB_ACTIONS=true` the
format is auto-selected, so the flag is documentary; the workflow command
emission is the same either way. Exit codes are `0` clean, `1` warnings,
`2` errors — wire `1` to advisory or hard-fail to taste.

## The eighteen-month-old shader folder

Open it. Run `hlsl-clippy lint --format=github-annotations` against the
hot pass. Read the rule doc on the first warning — every one of them
links to a blog post that explains the GPU mechanism. If the warning is
real on your target hardware, take the fix. If the compiler is folding it
on your specific SM target and you have measured that, suppress per-line:

```hlsl
float r = pow(roughness, 2.0); // hlsl-clippy: allow(pow-const-squared)
```

The rules are not snake oil. They will not make your shader 2x faster.
They will, in aggregate across a large codebase, recover the 0.3 ms you
did not know you were spending — and along the way, the doc pages will
explain enough about the hardware that you stop writing the patterns in
the first place. That second outcome is the one we actually care about.

---

`hlsl-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/hlsl-clippy](https://github.com/NelCit/hlsl-clippy).
If you have encountered a shader pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
