---
id: slang-interface-conformance-missing-method
category: slang-language
severity: error
applicability: none
since-version: v1.5.0
phase: 8
language_applicability:
  - slang
references:
  - title: "Slang User's Guide -- Interfaces and Extensions"
    url: "https://shader-slang.com/slang/user-guide/interfaces-generics.html"
  - title: "Slang Language Reference -- Conformance"
    url: "https://shader-slang.com/slang/language-reference/declarations.html"
---

# slang-interface-conformance-missing-method

> **Status:** shipped (Phase 8.C — ADR 0021 sub-phase C, v1.5.0).

## What it detects

An `extension Foo : IFoo {}` block whose body fails to implement every
method declared on the conformed-to interface. The rule walks the
tree-sitter-slang AST in two passes:

1. Collect every `interface_specifier` declared in the translation unit
   into an `(interface-name -> set-of-method-names)` map. A "method" is
   any `field_declaration` inside the interface's `field_declaration_list`
   body whose `declarator` field is a `function_declarator`.
2. Walk every `extension_specifier` node carrying a `base_class_clause`
   child (the `: IFoo` conformance form) and intersect the methods
   defined inside the extension's `field_declaration_list` body
   against the interface's required-method set. Every missing method
   fires an Error-severity diagnostic.

The rule is purely syntactic — no Slang reflection — so it runs without
`--reflect`. Interfaces declared in another module / `import`-ed
translation unit are skipped silently to avoid false positives. Note
on actual node-kinds: the prompt's predicted `interface_declaration` /
`extension_declaration` are NOT what tree-sitter-slang emits;
the grammar uses `interface_specifier` / `extension_specifier`
(documented in the rule body).

## Why it matters on a GPU

The cost here is correctness, not performance. Slang's diagnostic for
missing-method-conformance is silent at definition site — `extension
Foo : IFoo {}` with an empty body type-checks cleanly until a downstream
caller invokes one of the missing methods through a generic that
constrains its parameter to `IFoo`. By that point, the diagnostic
points at the call site rather than the broken extension definition,
and the developer has to mentally walk back through the inheritance
chain to find the actual cause.

Catching the breakage at definition time means the developer fixes the
right line of code on the first iteration. The cost on the GPU itself
is downstream — an extension that can't satisfy its interface contract
either crashes the Slang front-end at use-site (best case) or silently
binds to a different overload than the developer expected (worst case,
behavioural regression).

## Examples

### Bad

```slang
interface IFoo {
    float compute(float x);
    float reduce(float a, float b);
}
struct BadImpl { }

// Two diagnostics fire here: one per missing method.
extension BadImpl : IFoo {
}
```

### Good

```slang
interface IFoo {
    float compute(float x);
    float reduce(float a, float b);
}
struct GoodImpl { }

extension GoodImpl : IFoo {
    float compute(float x) { return x; }
    float reduce(float a, float b) { return a + b; }
}
```

## Options

none

## Fix availability

**none** — synthesising a method body requires developer intent
(behaviour of `compute()` is not derivable from the interface signature
alone). The diagnostic points at the extension and names the missing
methods.

## See also

- Companion blog post: link to the relevant per-category overview under
  `../blog/slang-language-overview.md` if one exists; otherwise leave as
  _not yet published_ until a per-rule post lands.

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/slang-interface-conformance-missing-method.md)
