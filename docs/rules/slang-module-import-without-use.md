---
id: slang-module-import-without-use
category: slang-language
severity: warn
applicability: suggestion
since-version: v1.5.0
phase: 8
language_applicability:
  - slang
references:
  - title: "Slang User's Guide -- Modules and Imports"
    url: "https://shader-slang.com/slang/user-guide/modules.html"
  - title: "hlsl-clippy unused-cbuffer-field rule"
    url: "https://nelcit.github.io/hlsl-clippy/rules/unused-cbuffer-field.html"
---

# slang-module-import-without-use

> **Status:** shipped (Phase 8.C — ADR 0021 sub-phase C, v1.5.0).

## What it detects

An `import Foo;` statement (tree-sitter-slang `import_statement` node)
whose tail-segment identifier `Foo` never appears anywhere else in the
source bytes outside the import statement itself. The detection is a
text-search heuristic mirroring the existing `unused-cbuffer-field`
rule: standalone-identifier occurrences are counted using `is_id_char`
boundaries so substrings (e.g. `FooBar`, `xFoo`) don't suppress the
diagnostic.

For dotted imports (`import Foo.Bar.Baz;`) the rule keys on the LAST
segment — that is what callers reference via `Baz.api()` or via
`Foo.Bar.Baz.api()`. Empirically the tree-sitter-slang grammar parses
dotted imports as an `import_statement` with multiple `identifier`
children; the rule walks the children and uses the trailing one as the
search key. The grammar uses `import_statement` (NOT
`import_declaration` as the prompt predicted) — the latter appears in
the C++ inheritance chain but is not the node-kind used for Slang's
`import` form.

## Why it matters on a GPU

Slang loads, parses, and type-checks every `import`-ed module
regardless of whether any of its symbols are referenced in the
importing translation unit. On a project with 50 shaders that all
`import Common; import Math; import Lighting;`, an unused import in a
heavily-used module bloats the per-shader compile budget by the
parse-and-typecheck cost of the imported tree. For a Slang module that
itself imports four other modules, the compile cost is multiplicative.

Removing dead imports is the cheapest single optimisation a Slang
codebase can take to reduce shader-compile latency on hot paths
(content-author iteration, shader cache misses on game launch).

## Examples

### Bad

```slang
import Lighting;        // Imported but never referenced below.

float test(float x) { return x; }
```

### Good

```slang
import Lighting;

float test(float x) {
    return Lighting.applyAmbient(x);
}
```

## Options

none

## Fix availability

**suggestion** — removing the import line is mechanical, but the rule
deliberately does not auto-apply. The text-search heuristic is high-
precision low-recall: a real "dead import" check needs module-aware
reflection (a module can re-export symbols whose names don't match the
module identifier, in which case the rule misses dead imports
entirely). Auto-deletion would be unsafe in those cases. The user is
expected to verify before applying.

## See also

- Companion blog post: link to the relevant per-category overview under
  `../blog/slang-language-overview.md` if one exists; otherwise leave as
  _not yet published_ until a per-rule post lands.
- Related rule: [unused-cbuffer-field](unused-cbuffer-field.md).

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/slang-module-import-without-use.md)
