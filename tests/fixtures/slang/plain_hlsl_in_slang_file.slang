// Fixture (a) per ADR 0020 sub-phase A: plain-HLSL content with `.slang`
// extension. Demonstrates that AST-rule patterns inside the file do NOT
// fire (the orchestrator skips tree-sitter-hlsl dispatch on `.slang`),
// even though the same pattern would fire on `.hlsl`.
//
// `pow(x, 2.0)` would normally fire pow-const-squared on a `.hlsl` file.
// Here we expect ZERO `pow-const-squared` diagnostics on this fixture
// because the AST stage is skipped.
//
// Expected output: exactly one `clippy::language-skip-ast` Note plus any
// reflection-stage diagnostics Slang's frontend can compute on this file.

// SHOULD-NOT-HIT(pow-const-squared) -- AST stage skipped on .slang
float squared(float x) {
    return pow(x, 2.0);
}
