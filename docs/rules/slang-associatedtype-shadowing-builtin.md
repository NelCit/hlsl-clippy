---
id: slang-associatedtype-shadowing-builtin
category: slang-language
severity: warn
applicability: none
since-version: v1.5.0
phase: 8
language_applicability:
  - slang
references:
  - title: "Slang User's Guide -- Associated Types"
    url: "https://shader-slang.com/slang/user-guide/interfaces-generics.html"
  - title: "HLSL Specification -- Built-in Types"
    url: "https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-data-types"
---

# slang-associatedtype-shadowing-builtin

> **Status:** shipped (Phase 8.C — ADR 0021 sub-phase C, v1.5.0).

## What it detects

An `associatedtype X;` declaration inside an `interface_specifier`
body whose name `X` collides with a built-in HLSL/Slang type name. The
rule walks the tree-sitter-slang AST looking for
`associatedtype_declaration` nodes (the grammar's exact node-kind, NOT
the prompt-predicted `associatedtype_declaration` shape — they happen
to match). The first `type_identifier` child carries the associated
type's name; the rule checks it against a built-in vocabulary including
the scalar primitive types (`bool`, `int`, `uint`, `float`, `double`,
`half`, `min16float`, etc.), the common vector shorthand families
(`float2`, `float3`, `float4`, ...), the texture surfaces (`Texture2D`,
`Texture3D`, `TextureCube`, ...), the buffer surfaces (`Buffer`,
`StructuredBuffer`, `RWBuffer`, `ByteAddressBuffer`, ...), and the
sampler / acceleration-structure types.

## Why it matters on a GPU

This is a name-resolution footgun that costs correctness, not
performance. Inside an interface scope, Slang's name resolution prefers
the abstract associated type over the surrounding built-in type. So a
function body inside the interface that references `Texture2D`
implicitly binds to the abstract associated type — every call to
`Texture2D::Sample` etc. routes through a witness table lookup at
specialisation time, and the developer's intent ("I want HLSL's
concrete `Texture2D` here") is silently discarded.

The fix is always to rename the associated type. The cost is exactly
two characters of extra typing (e.g. `Tex2D` instead of `Texture2D`)
and a much clearer reading of what the interface actually requires
of its conformers.

## Examples

### Bad

```slang
interface IBad {
    // Inside this interface, `Texture2D` no longer means the HLSL
    // built-in -- it means "whatever concrete type the conformer
    // chooses to substitute". Subtle and silent.
    associatedtype Texture2D;
}
```

### Good

```slang
interface IGood {
    // Different name -- no shadowing. References to `Texture2D`
    // inside the interface body still mean the HLSL built-in.
    associatedtype Tex;
}
```

## Options

none

## Fix availability

**none** — picking a non-shadowing name requires developer intent.
The diagnostic suggests renaming.

## See also

- Companion blog post: link to the relevant per-category overview under
  `../blog/slang-language-overview.md` if one exists; otherwise leave as
  _not yet published_ until a per-rule post lands.

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/slang-associatedtype-shadowing-builtin.md)
