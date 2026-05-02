// Fixture (c) per ADR 0020 sub-phase A, updated for ADR 0021 sub-phase B
// (v1.4.0): Slang-only constructs (generic + interface). tree-sitter-hlsl
// could not parse these (Slang's `__generic`, `interface`, `associatedtype`
// productions). Under sub-phase B, tree-sitter-slang parses them; AST
// rule dispatch runs over the resulting tree.
//
// Whether `pow-const-squared` fires here is empirically observed — the
// rule's match depends on whether the call inside `compute()` surfaces
// as a `call_expression` once the surrounding generic / `let` syntax
// is parsed. The unit test (`B.4 — Slang fixtures on disk dispatch
// through tree-sitter-slang`) accepts 0 or 1 fires here as both being
// part of the coarse audit; the fine-grained audit in v1.4.x catalogs
// the exact AST shape the grammar produces around `let` declarations.
//
// Expected output: zero `clippy::language-skip-ast` notices.

interface IDifferentiable {
    associatedtype Differential;
    static Differential dzero();
}

__generic<T : IDifferentiable>
T multiply_self(T value) {
    return value;
}

float compute(float x) {
    let y = multiply_self<float>(x);
    return pow(y, 2.0);
}
