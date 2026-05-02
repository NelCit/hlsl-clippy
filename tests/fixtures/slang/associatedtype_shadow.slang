// ADR 0021 sub-phase C — fixture for
// `slang-associatedtype-shadowing-builtin`.
//
// `associatedtype X` whose name X collides with a built-in HLSL/Slang
// type (e.g. `float`, `Texture2D`, `Buffer`) silently shadows the
// built-in inside the interface scope. Code that references the name
// inside the interface body binds to the abstract associated type
// rather than the concrete built-in.

interface ISafe {
    // SHOULD-NOT-HIT(slang-associatedtype-shadowing-builtin) -- "Differential"
    // is not a built-in name.
    associatedtype Differential;
}

interface IBad {
    // HIT(slang-associatedtype-shadowing-builtin) -- shadows `Texture2D`.
    associatedtype Texture2D;
    // HIT(slang-associatedtype-shadowing-builtin) -- shadows `Buffer`.
    associatedtype Buffer;
}
