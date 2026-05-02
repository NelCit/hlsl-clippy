// ADR 0021 sub-phase C — fixture for
// `slang-interface-conformance-missing-method`.
//
// An `extension Foo : IFoo` block that declares conformance to `IFoo`
// must implement every method on the interface. Slang's diagnostic for
// missing-method-conformance is silent at definition site -- it only
// surfaces when a downstream caller invokes the missing method, by
// which time the breakage is far from its cause.

interface IFoo {
    float compute(float x);
    float reduce(float a, float b);
}

struct GoodImpl { }
struct BadImpl  { }

// HIT(slang-interface-conformance-missing-method) -- empty body, both
// methods missing. Two diagnostics fire: one per missing method.
extension BadImpl : IFoo {
}

// SHOULD-NOT-HIT(slang-interface-conformance-missing-method) -- both
// methods implemented.
extension GoodImpl : IFoo {
    float compute(float x) { return x; }
    float reduce(float a, float b) { return a + b; }
}
