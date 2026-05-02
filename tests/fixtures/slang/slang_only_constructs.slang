// Fixture (c) per ADR 0020 sub-phase A: Slang-only constructs that would
// produce ERROR nodes if the orchestrator ran tree-sitter-hlsl on this
// source. Includes a generic + an interface — both syntactic constructs
// tree-sitter-hlsl cannot parse. By skipping the AST stage entirely on
// `.slang`, these surface only via Reflection (which Slang's native
// frontend handles cleanly).
//
// Expected output: exactly one `clippy::language-skip-ast` Note. No AST
// diagnostics, no parser-error spew.

// SHOULD-NOT-HIT(pow-const-squared) -- AST stage skipped on .slang

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
