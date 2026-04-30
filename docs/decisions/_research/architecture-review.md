<!--
date: 2026-04-30
prompt-summary: review proposed C++ architecture for hlsl-clippy — module decomposition, external deps, public API boundaries, risks, and the cross-cutting decisions to lock in before rule writing begins.
preserved-verbatim: yes — see ../0001-compiler-choice-slang.md, ../0002-parser-tree-sitter-hlsl.md, ../0003-module-decomposition.md for the distilled decisions.
-->

# Architectural review: hlsl-clippy

## 1. Module decomposition

Drop `crates/` (Rust-ism). Split `core` into focused static libraries so the rule engine never transitively pulls Slang headers into a tree-sitter-only translation unit, and so the future LSP can link the same artifacts as the CLI.

Proposed layout:

```
hlsl-clippy/
  CMakeLists.txt              # top-level orchestration only
  cmake/                      # FindSlang.cmake, warning flags, sanitizer toggles
  third_party/                # vendored sources (see §2)
  include/hlslc/              # PUBLIC headers, namespace hlslc::
    source.hpp  span.hpp  diagnostic.hpp  fix.hpp
    rule.hpp  rule_registry.hpp  config.hpp  driver.hpp
  libs/
    parser/      (hlslc_parser)      # tree-sitter wrapper, query helpers
    semantic/    (hlslc_semantic)    # Slang session, reflection bridge
    diag/        (hlslc_diag)        # Diagnostic, Fix, Rewriter, formatters
    rules/       (hlslc_rules)       # individual rules + registry glue
    driver/      (hlslc_driver)      # pipeline: source -> parse -> compile -> rules
  apps/
    cli/         (hlsl-clippy)       # main.cpp, arg parsing, glob, exit codes
    lsp/         (hlsl-clippy-lsp)   # JSON-RPC, document sync (Phase 5)
  tests/
    unit/  corpus/  golden/
```

Rationale: `rules` depends on `parser`+`semantic`+`diag`, never the reverse. `cli` and `lsp` both depend only on `driver`. Each rule lives in `libs/rules/<category>/<rule_name>.cpp` and self-registers via a static initializer collected in `rule_registry.cpp`, so adding rule #47 is one file plus one CMake glob.

The `src/` directory and the `crates/` tree should be deleted; `src/main.cpp` moves to `apps/cli/main.cpp`. Update the `HeaderFilterRegex` in `.clang-tidy` from `^(cli|core|src)/...` to `^(apps|libs|include)/...`.

## 2. External dependencies

- **tree-sitter (runtime + tree-sitter-hlsl grammar)**: vendor as **git submodules** under `third_party/tree-sitter/` and `third_party/tree-sitter-hlsl/`. The grammar is small C, you'll patch it (ROADMAP open-question explicitly anticipates this), and submodules make patches reviewable. Build both as **OBJECT** libraries linked statically into `hlslc_parser`. Don't `find_package` — distros don't ship tree-sitter-hlsl.
- **Slang**: **git submodule** under `third_party/slang/` with a pinned tag, built via `add_subdirectory` with `SLANG_ENABLE_TESTS=OFF`, `SLANG_LIB_TYPE=STATIC` if their build supports it. FetchContent is tempting but Slang has its own submodules (glslang, spirv-tools) and large checkout — reproducible CI wants a frozen submodule with a known hash. Provide a `cmake/FindSlang.cmake` shim that prefers a pre-built Slang via `HLSLC_SYSTEM_SLANG=ON` for fast iteration; submodule path is the default and the only one CI uses.
- **GSL (Microsoft)**: header-only, `FetchContent` is fine.
- **fmt** (for diagnostic rendering) and a JSON lib (for `--format=json` and LSP): `FetchContent_Declare` `fmtlib/fmt` and `nlohmann/json`. Both are header-stable.
- **Linkage**: static everywhere for the CLI (single-binary distribution per ROADMAP). If Slang's static build is broken on a target, fall back to shipping `slang.dll`/`libslang.so` alongside — but treat that as a release-engineering concern, not an architectural one.

## 3. API boundaries

`include/hlslc/` is the only header set `apps/` may include. Everything in `libs/*/src/` is private.

Source/span types must be agnostic of both backends — never expose `TSNode` or `slang::IComponentType*` in public headers:

```cpp
namespace hlslc {
struct SourceId { uint32_t v; };          // index into SourceManager
struct ByteSpan { uint32_t lo, hi; };     // half-open, UTF-8 byte offsets
struct Span { SourceId src; ByteSpan range; };

struct Fix {
    std::string label;
    std::vector<TextEdit> edits;          // disjoint, sorted, same SourceId
    enum class Applicability { MachineApplicable, MaybeIncorrect, HasPlaceholders };
    Applicability applicability;
};
struct Diagnostic {
    std::string code;                      // "pow-const-squared"
    Severity sev; Span primary;
    std::string message;
    std::vector<Span> notes;
    std::vector<Fix> fixes;                // 0..N
};
}
```

The `Rule` interface should support both visitor and query styles by being callback-driven, not visitor-typed:

```cpp
class RuleContext;                         // emit(), source(), reflection()
class Rule {
public:
    virtual ~Rule() = default;
    virtual std::string_view id() const = 0;
    virtual Category category() const = 0;
    virtual Stage stage() const = 0;       // Ast | Reflection | Flow | Ir
    // One of these is implemented; default no-ops.
    virtual void register_queries(QuerySet&) {}        // declarative path
    virtual void on_node(const AstCursor&, RuleContext&) {}  // imperative
    virtual void on_function(const FunctionInfo&, RuleContext&) {}  // reflection
};
```

The driver runs queries in one tree-walk per file (batched), then dispatches imperative `on_node` only for rules that opted out of queries. This keeps the common case (most Phase-2 rules) declarative without forcing it on flow-analysis rules.

## 4. Risks

- **Span ↔ reflection mapping**: tree-sitter gives byte spans; Slang reflection gives `SourceLoc` referencing its own source manager. Build a `SourceManager` in `core` that owns the canonical UTF-8 buffer and hands the same `SourceId` to both backends — re-feed Slang the exact bytes tree-sitter parsed (no re-reading the file). Map Slang `SourceLoc` → `(line,col)` → byte offset via a precomputed line-offset table per `SourceId`. Cache this table; it's hot.
- **Error propagation**: use `std::expected<T, ParseError>` (or `tl::expected` until C++23 lands cleanly on MSVC) at every stage boundary. A parse failure must still allow rules that only need partial trees (tree-sitter is error-recovering) to run. Compile failure should fall back to AST-only rules — never abort the whole file.
- **Threading**: `Rule` instances must be `const`-callable and stateless; per-file mutable state lives on `RuleContext`. Parallelize **across files** (`std::for_each(std::execution::par, ...)`), single-threaded within a file. Slang sessions are documented as not thread-safe — keep one `slang::IGlobalSession` shared, but a `slang::ISession` per worker thread, pooled.
- **Slang ABI churn**: never include `<slang.h>` from `include/hlslc/`. All Slang types are forward-declared inside `libs/semantic/` only. A single `SemanticView` POD crosses the boundary. When Slang bumps, only `libs/semantic/` recompiles.
- **Memory ownership**: `TSTree*` is owned by a `ParsedTree` RAII wrapper (`std::unique_ptr` with custom deleter). Slang's `ComPtr<>`-style refcounted objects stay inside `libs/semantic/`. Public headers expose only value types and `std::span<const Diagnostic>`.

## 5. Decisions to lock in before rule writing

1. Public-header backend isolation. No `tree_sitter/api.h` or `slang.h` in `include/hlslc/`. Enforce with a CI grep.
2. Span representation = `(SourceId, byte-lo, byte-hi)`. UTF-8 bytes, half-open. Changing this later rewrites every rule and every fix.
3. Diagnostic + Fix schema, including the `Applicability` enum. Serialization (text, JSON, LSP) keys off it. Adding a variant later is fine; renaming one breaks every consumer.
4. `Rule` interface shape (queries + optional imperative + stage tag). Plus the static self-registration mechanism — retrofitting registration across 80 rules is grim.
5. `SourceManager` as the single source of truth fed to both tree-sitter and Slang. Decide now that file I/O happens exactly once per file per run.
6. Suppression syntax (`// hlsl-clippy: allow(rule-name)`) and how it's parsed — line comments must be tracked by the parser bridge from day one, since rules can't retroactively know about comments tree-sitter discarded.
7. Threading contract: rules are stateless and `const`-invocable; parallelism granularity is the file. Documented in `rule.hpp`.
8. Slang version pin + upgrade policy as a top-level `cmake/SlangVersion.cmake` with a single `SLANG_REVISION` variable; CI matrix can override to test next-rev.
