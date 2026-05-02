// ADR 0021 sub-phase C — fixture for `slang-generic-without-constraint`.
//
// `__generic<T>` with no `: ICompute`-style interface constraint defers
// monomorphisation to use-site rather than specialising at compile time.
// The constrained form lets the compiler specialise per concrete T.

interface ICompute {
    float compute(float x);
}

// HIT(slang-generic-without-constraint) -- bare type parameter, no bound.
__generic<T>
void f(T x) { }

// SHOULD-NOT-HIT(slang-generic-without-constraint) -- constrained.
__generic<T : ICompute>
void g(T x) { }
