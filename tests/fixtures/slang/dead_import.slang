// ADR 0021 sub-phase C — fixture for `slang-module-import-without-use`.
//
// `import Foo;` whose `Foo` identifier never appears anywhere else in
// the source bloats compile time -- Slang still loads, parses, and
// type-checks the imported module. The text-search heuristic is
// high-precision low-recall: it catches the common
// "added an import and never wrote any code that uses it" case
// without false positives on real `Bar.api()` usage.

// HIT(slang-module-import-without-use) -- the imported name is never
// referenced elsewhere in this translation unit.
import DeadModule;

// SHOULD-NOT-HIT(slang-module-import-without-use) -- the import target
// is referenced in the body below via Live.helper(...).
import Live;

float test(float x) {
    return Live.helper(x);
}
