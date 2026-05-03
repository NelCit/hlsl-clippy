---
id: slang-generic-without-constraint
category: slang-language
severity: warn
applicability: suggestion
since-version: v1.5.0
phase: 8
language_applicability:
  - slang
references:
  - title: "Slang User's Guide -- Generics"
    url: "https://shader-slang.com/slang/user-guide/generics.html"
  - title: "Slang User's Guide -- Interfaces and Constraints"
    url: "https://shader-slang.com/slang/user-guide/interfaces-generics.html"
---

# slang-generic-without-constraint

> **Status:** shipped (Phase 8.C — ADR 0021 sub-phase C, v1.5.0).

## What it detects

A `__generic<T>` declaration whose type parameter list contains no
interface-conformance constraint (no `: ITrait` clause on any parameter).
The rule walks the tree-sitter-slang AST looking for a top-level
`template_type` node whose `name` field has text `__generic` and whose
`template_argument_list` carries zero `interface_requirements`
children. The constrained form (`__generic<T : ICompute>`) parses with an
`interface_requirements` sibling next to the `type_descriptor` for `T`,
which suppresses the rule.

Note on actual node-kinds: tree-sitter-slang does not emit a dedicated
`generic_parameter_list` node-kind. `__generic<T>` is parsed as a
`template_type` whose `name` is the verbatim `__generic` identifier
followed by a `template_argument_list`. The detection logic anchors on
that empirical shape (probed during sub-phase C development).

## Why it matters on a GPU

Slang's compile-time specialisation depends on knowing the abstract
operations available on every type parameter. With a constraint
(`T : ICompute`), the front-end can specialise the body of the generic
at every concrete call site, inlining `ICompute::compute` directly and
emitting straight-line code. Without a constraint, Slang has to emit
the generic body once and dispatch per-instantiation through a runtime
witness table. Even when the back-end manages to inline the witness
calls, the IR carries the indirection until the optimiser proves
convergence — which costs instruction-cache footprint and constrains
register-allocation decisions earlier than necessary.

The cost is most visible in shader libraries that ship a single
`__generic<T>` "do this with anything" body called from dozens of
specialised entry points: every concrete entry point pays the
instruction-cache cost of the unspecialised body, even when the
back-end specialises locally.

## Examples

### Bad

```slang
__generic<T>
void process(T value) {
    // Slang has no idea what `T` supports, so the body specialises
    // late and the IR carries witness-dispatch indirection.
}
```

### Good

```slang
interface ICompute {
    float compute(float x);
}

__generic<T : ICompute>
void process(T value) {
    // Front-end specialises per concrete `T`, inlining `compute`.
}
```

## Options

none

## Fix availability

**suggestion** — adding the right interface bound requires a developer
decision (which interface, which conformance set). The diagnostic
points at the construct and explains the form to add; no automated
rewrite is offered.

## See also

- Companion blog post: link to the relevant per-category overview under
  `../blog/slang-language-overview.md` if one exists; otherwise leave as
  _not yet published_ until a per-rule post lands.

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/slang-generic-without-constraint.md)
