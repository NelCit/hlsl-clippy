// Fixture (a) per ADR 0020 sub-phase A, updated for ADR 0021 sub-phase B
// (v1.4.0): plain-HLSL content with `.slang` extension. Under sub-phase B
// the orchestrator routes `.slang` files through tree-sitter-slang. That
// grammar inherits tree-sitter-hlsl, so plain-HLSL bodies parse identically
// and AST-rule patterns DO fire — the v1.3.x quarantine is lifted.
//
// `pow(x, 2.0)` fires pow-const-squared on this fixture under tree-
// sitter-slang dispatch, identically to the `.hlsl` path.
//
// Expected output: zero `clippy::language-skip-ast` notices (parser
// dispatch lit up the AST stage) and exactly one `pow-const-squared`
// diagnostic from the body below.

// HIT(pow-const-squared) -- AST runs on .slang under tree-sitter-slang
float squared(float x) {
    return pow(x, 2.0);
}
