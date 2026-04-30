# Vendored Tree-Sitter Versions

## tree-sitter (runtime + C API)

| Field | Value |
|-------|-------|
| Repository | https://github.com/tree-sitter/tree-sitter |
| Tag | `v0.26.8` |
| Commit | `cd5b087cd9f45ca6d93ab1954f6b7c8534f324d2` |
| Released | 2026-03-31 |
| ABI version exposed | `TREE_SITTER_LANGUAGE_VERSION = 15` |
| Min compatible grammar ABI | `TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION = 13` |
| Build method | Single unity source `lib/src/lib.c` (internally `#include`s all other `.c` files) |

## tree-sitter-hlsl (HLSL grammar)

| Field | Value |
|-------|-------|
| Repository | https://github.com/tree-sitter-grammars/tree-sitter-hlsl |
| Version | `0.2.0` |
| Commit | `bab9111922d53d43668fabb61869bec51bbcb915` |
| Grammar ABI | `LANGUAGE_VERSION = 14` (compatible with runtime min = 13) |
| Source files | `src/parser.c`, `src/scanner.c` (external scanner for complex tokens) |

## Grammar Coverage Gaps Observed

The following HLSL constructs in `tests/fixtures/clean.hlsl` produce `ERROR` nodes
in the parse tree (i.e., `ts_node_has_error()` returns `true`):

### cbuffer with register binding and body

```hlsl
cbuffer Frame : register(b0) {
    float4x4 ViewProj;
    ...
};
```

The grammar has partial `cbuffer` support but does not correctly parse the
`: register(b0)` binding annotation on a `cbuffer` declaration, resulting in
an ERROR subtree at that point. The member declarations inside may parse
correctly depending on how the parser recovers.

### Attribute syntax (numthreads)

```hlsl
[numthreads(8, 8, 1)]
void cs_post(...) { ... }
```

The `[numthreads(...)]` attribute bracket syntax is partially covered but
parsing the full combination with the function declaration may still produce
error nodes in some versions.

### Semantic annotations on struct members

```hlsl
float3 pos : POSITION;
```

Colon-delimited semantic annotations on struct member declarations are
generally well-supported, but edge cases around function return semantics
(`: SV_Target`) may have limited coverage.

### Decision

These grammar gaps are **noted but not fixed** in this task per the task
specification. The smoke test exits 0 even when `has_error` is true, but
prints a warning to stderr describing the known gaps and referencing this file.

If full HLSL parse coverage is needed, consider:
- Upstream contributions to tree-sitter-hlsl
- Patching `grammar.js` locally and re-generating `parser.c`
- Using the DXC / clang-based HLSL AST instead of tree-sitter for semantic passes
