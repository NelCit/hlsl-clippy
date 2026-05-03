# Architecture

A high-level guide to the `shader-clippy` internals for new contributors. For the full phase plan see [ROADMAP](../ROADMAP.md).

## Pipeline

```
.hlsl source ──► tree-sitter parse ──► Slang compile + reflect
                       │                       │
                       └────► Rule engine ◄────┘
                                    │
                       ┌────────────┼────────────┐
                       ▼            ▼            ▼
                 CLI text     JSON output    LSP diagnostics
```

## Components

### SourceManager

Owns the canonical UTF-8 buffer for every file in the lint session. All components that need file content borrow a `gsl::span<const char>` from `SourceManager`; nothing else holds raw file bytes. `SourceManager` also maintains line-offset tables so that byte offsets can be converted to `(line, column)` pairs for diagnostics.

### tree-sitter parse

`tree-sitter-hlsl` produces a concrete syntax tree from the source buffer. The tree covers every token — including comments, which carry the inline suppression annotations (`// shader-clippy: allow(...)`). The parse is incremental: when the LSP server receives a file-change notification, only the changed region is re-parsed. Each node carries a byte-range `[start, end)` that maps directly into `SourceManager`'s buffer.

The tree is the primary substrate for Phase 2 (AST-only) rules. It is also the span source for all later phases: even when a rule fires on a Slang reflection property, the diagnostic span points to the tree-sitter node that corresponds to the offending source construct.

### Slang compile + reflect

The same source buffer is handed to Slang's compilation API. Slang validates the shader (reporting compile errors), resolves `#include` paths, and exposes a reflection API that covers:

- Shader stage and entry point attributes
- Resource bindings: `register`, `space`, descriptor sets
- `cbuffer` / `ConstantBuffer<T>` layouts including field offsets and packing holes
- `[numthreads]` X/Y/Z values
- Type information: base type, component count, array dimensions

Slang also emits DXIL and SPIR-V. The compiled IR is used by Phase 7 IR-level rules (VGPR pressure, redundant samples, etc.) but is not consumed by Phases 2–4.

### Rule engine

The rule engine maintains a registry of `Rule` objects. Each `Rule` implements one or more visitor hooks:

- `on_node(const ts::Node&)` — called for every tree-sitter node (s-expression query helpers narrow which node types trigger each rule)
- `on_reflect(const slang::ShaderReflection&)` — called once after Slang reflection completes

The engine drives a depth-first traversal of the tree-sitter CST, dispatching to registered visitors. It also checks inline suppression comments and filters out diagnostics that are suppressed at the call site, file, or config level.

### Diagnostic + Fix

Each rule emits zero or more `Diagnostic` values:

```
Diagnostic {
    rule_id:   string          // e.g. "pow-to-mul"
    severity:  Severity        // Allow | Warn | Deny
    span:      ByteRange       // into SourceManager buffer
    message:   string
    fix:       Option<Fix>
}
```

A `Fix` is a list of `TextEdit` values (offset, length, replacement string), enough for a range-based source rewriter. Machine-applicable fixes are applied by `shader-clippy fix`; suggestion-level fixes are shown but not auto-applied.

### Output formatters

Three formatters consume the `Diagnostic` list:

- **CLI text** — human-readable, rustc/clang-style with caret underlines. Default when stdout is a terminal.
- **JSON output** — array of diagnostic objects (see [CI integration](ci.md) for schema). Selected with `--format=json`.
- **LSP diagnostics** — converted to `lsp::Diagnostic` objects and pushed to the editor over JSON-RPC. Phase 5.
