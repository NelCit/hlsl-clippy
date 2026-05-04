---
id: repeated-pure-intrinsic
category: math
severity: warn
applicability: suggestion
since-version: v2.0.3
phase: 2
language_applicability:
  - hlsl
  - slang
references:
  - title: "HLSL Intrinsic Functions reference"
    url: "https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-intrinsic-functions"
  - title: "DXC optimisation passes — common subexpression elimination"
    url: "https://github.com/microsoft/DirectXShaderCompiler/wiki/Optimization"
---

# repeated-pure-intrinsic

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Two or more syntactically-identical calls to an expensive pure intrinsic
within the same function body, when no intervening mutation could have
changed the argument's value. Allowlist:

`sqrt`, `rsqrt`, `length`, `normalize`, `pow`, `exp`, `exp2`, `log`,
`log2`, `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`,
`sinh`, `cosh`, `tanh`.

Cheap intrinsics (`min`, `max`, `abs`, `dot`, `lerp`, `clamp`, `step`,
`smoothstep`, `saturate`) are intentionally excluded — the duplication
signal is too noisy and the cost too low to be worth flagging.

The detector tracks three classes of mutation between two candidate
calls A and B and skips the report if any apply to the argument:

1. **Direct assignment** to an identifier referenced in the argument
   list (`x = ...`, `x += ...`, `x.field = ...`).
2. **Increment / decrement** of such an identifier (`x++`, `--x`).
3. **Calls to non-allowlisted functions** that pass any argument
   identifier — conservatively treated as a wildcard barrier because
   HLSL `out` / `inout` parameters cannot be detected from the AST
   alone.

## Why it matters on a GPU

Modern HLSL / Slang compilers (DXC, Slang's downstream DXIL / SPIR-V
emitters) already perform common-subexpression elimination on pure
intrinsics at `-O1`, so the runtime cost of the duplicate is usually
zero — the compiler hoists it for you. The rule's value is **clarity
and intent**, not raw performance:

- Duplicated calls signal confused authorship: the developer either
  forgot they already computed the value, or copy-pasted a sub-expression
  without consolidating.
- Hand-written breakpoint placement, SSA inspection, and shader-debugger
  stepping all behave better when the call appears once with a named
  result.
- Fast-math / FMA folding tweaks (e.g. `precise`, `[FastMath]` attribute
  variants) are easier to reason about with a single named call.
- On older / lower-quality compiler pipelines (mobile vendor toolchains,
  cross-compilers with weaker CSE), the duplicate may actually emit
  twice — flagging at the source level is cheap insurance.

## Examples

### Bad

```hlsl
float ior_index(float f0, float maxIor)
{
    float iorIndex = 1.0;
    if (f0 < 1)
    {
        // sqrt(f0) computed twice in the same expression.
        float ior = (sqrt(f0) + 1.0f) / (1.0f - sqrt(f0));
        iorIndex = saturate((ior - 1.0f) / (maxIor - 1.0f));
    }
    return iorIndex;
}
```

### Good

```hlsl
float ior_index(float f0, float maxIor)
{
    float iorIndex = 1.0;
    if (f0 < 1)
    {
        float s = sqrt(f0);
        float ior = (s + 1.0f) / (1.0f - s);
        iorIndex = saturate((ior - 1.0f) / (maxIor - 1.0f));
    }
    return iorIndex;
}
```

### Not flagged (mutation between calls)

```hlsl
float foo(float x)
{
    float a = sqrt(x);
    x = x * 2.0;        // x changed — calls are NOT duplicates
    float b = sqrt(x);
    return a + b;
}
```

## Options

none

## Fix availability

**suggestion** — Hoisting is mechanical but the rule does not auto-rewrite
because (a) the new local needs a name (`s`? `sqrtF0`?), (b) hoisting
above a branch may move work that was guarded for a reason, and
(c) precision-sensitive ordering may rely on the duplicate computation
in rare numerical-analysis code. We surface the duplicate and let the
developer confirm.

## Limitations

- **Lexical-scope shadowing.** Function-scope detection treats all
  references to the same identifier name as the same value, so a
  pathological shadowing pattern (`float x; sqrt(x); { float x; sqrt(x); }`)
  may produce a false positive. Suppress with
  `// shader-clippy: allow(repeated-pure-intrinsic)` if encountered.
- **Aliased mutation through pointers / structures.** HLSL has no
  pointer aliasing in user code, but a `cbuffer` or `RWStructuredBuffer`
  field accessed through different paths may be silently mutated by an
  intervening `Load` / `Store`. The rule does not track these — false
  positives are possible. Same suppression workaround applies.

## See also

- HLSL intrinsic reference:
  [Intrinsic Functions](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-intrinsic-functions)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/repeated-pure-intrinsic.md)
