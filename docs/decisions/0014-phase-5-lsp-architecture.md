---
status: Accepted
date: 2026-05-01
deciders: NelCit
tags: [phase-5, lsp, vscode, ide, distribution, macos, infrastructure, planning]
---

# Phase 5 LSP + IDE architecture — JSON-RPC server + VS Code extension

## Context and Problem Statement

The CLI shipped in Phases 0/1 (and the rule packs queued through Phases 2 /
3 / 4) is the CI gate — it runs to a non-zero exit code, renders
diagnostics, and applies `--fix` rewrites unattended. That is the right
shape for `pre-commit`, GitHub Actions, and `make lint` integrations, but
it is the wrong shape for the developer loop. Authors editing an HLSL file
in VS Code want diagnostics squiggled inline as they type, code actions
that apply quick-fixes from the lightbulb menu, hover tooltips that link
to the rule documentation, and per-document config that follows the
`.shader-clippy.toml` workspace boundary the CLI already understands.

Per `ROADMAP.md` §"Phase 5 — Ergonomics: LSP + IDE":

> - LSP server (small JSON-RPC layer; reuse the diagnostic + fix engine)
> - VS Code extension (thin wrapper around LSP)
> - Quick-fix surfaced as VS Code code actions
> - Workspace-aware: respects `.shader-clippy.toml`, multi-root projects
> - Slang module / `#include` resolution wired into LSP

Per CLAUDE.md "macOS CI deferred to Phase 5" and ADR 0005 §"Runners +
toolchains" — Phase 5 is also when macOS finally enters the CI matrix.
That is not a coincidence. Marketplace publishing for the VS Code
extension implies a macOS distribution target, and the LSP server has to
demonstrably build + run there.

This ADR proposes the smallest viable architecture that hits all five
ROADMAP bullets, brings macOS into CI, and respects every locked
constraint already in the codebase:

1. **`<slang.h>` MUST NOT appear in `core/include/shader_clippy/`.** ADR
   0001 + CLAUDE.md "What NOT to do". The LSP frontend MUST not introduce
   a fresh leak of Slang types through any new header it adds.
2. **No exceptions across the `core` API boundary.** ADR 0004 + CLAUDE.md
   "Code standards — Ban list". The JSON-RPC dispatcher cannot let parse
   errors propagate as exceptions into `core::lint()`.
3. **Apache-2.0 across every shipped artifact.** ADR 0006 — code is
   Apache-2.0; the VS Code extension is code; therefore the extension is
   Apache-2.0 as well.
4. **Single-binary distribution per OS.** ADR 0005 — the CLI release
   workflow uses `softprops/action-gh-release@v2`; the LSP binary MUST
   slot into the same workflow without a parallel CPack track.
5. **Slang `IGlobalSession` is not thread-safe.** ADR 0001 + ADR 0012.
   Long-lived process is a feature here — keep the global session warm
   across requests; dispatch lint runs onto the per-worker `ISession`
   pool that ADR 0012 already specifies.

This ADR is a **plan**, in the same shape as ADR 0008 (Phase 1) and ADR
0012 (Phase 3 reflection infrastructure). **No code is written by this
ADR.** Sub-phases below specify what implementation PRs land in what
order; the implementation work itself happens after the ADR is moved to
Accepted.

## Decision Drivers

- **Reuse `core` library — the LSP is a thin frontend.** No rule logic
  moves out of `core`; no new rule API surface; the LSP simply translates
  LSP-protocol requests into `lint(SourceManager, SourceId, rules,
  Config, path)` calls and translates `Diagnostic` / `Fix` back out into
  LSP-protocol responses. This is what keeps the rule-pack work in
  Phases 2/3/4 from blocking on Phase 5 infrastructure.

- **Workspace-awareness.** `.shader-clippy.toml` is resolved per-document
  via the existing `find_config()` walk-up (bounded by the first
  `.git/` ancestor; see `core/include/shader_clippy/config.hpp`). The LSP
  server reuses that resolver verbatim — no new resolution logic. A
  file-watcher on `.shader-clippy.toml` invalidates the cached `Config`
  for every open document under that config root.

- **Latency budget.** <100 ms p50 for incremental re-lint of a saved
  buffer; <500 ms p95. Slang reflection on a cold cache (first compile
  of a source) can exceed 500 ms — surface a "linting…" status while
  reflection runs the first time, then hit the budget for every
  subsequent re-lint via the per-`(SourceId, target_profile)` cache
  ADR 0012 already specifies. AST-only rule packs (Phase 0/1/2) hit
  the budget cold, because the reflection engine never constructs
  for those packs (per ADR 0012's "lazy invocation" driver).

- **Long-lived process.** The CLI starts and exits per invocation; the
  LSP runs for the duration of a VS Code session. That is the entire
  point: the global Slang session, the parser, the rule registry, and
  the reflection cache stay warm. The per-invocation Slang spin-up
  cost (which dominates CLI cold-start) amortises to zero across an
  editing session.

- **Apache-2.0 for the LSP server; Apache-2.0 for the VS Code extension.**
  Per ADR 0006: code is Apache-2.0. The extension's `package.json`
  declares `"license": "Apache-2.0"`; the extension repository directory
  ships its own `LICENSE` symlink (or copy on filesystems that lack
  symlink support — Windows-friendly) pointing to the repo-root
  Apache-2.0 text. No CC-BY-4.0 surface inside the extension; CC-BY
  applies only to docs/blog content per ADR 0006.

- **Cross-platform.** Linux + Windows + macOS. Phase 5 brings macOS
  into CI for the first time; the LSP server is what forces the issue
  (Marketplace publishes per-platform binaries; we cannot dodge macOS
  the way the CI-only CLI did through Phases 0–4).

- **Distribution.** VS Code Marketplace + GitHub Releases. The CLI
  artifact pipeline (per ADR 0005) extends to ship a second per-platform
  binary (`shader-clippy-lsp`) alongside the CLI; the extension binary
  bundles or lazily downloads them on first activation.

- **Vendor-neutral LSP.** The server speaks plain LSP (3.17). VS Code
  is the first client we ship, but the protocol contract is the
  long-term stable surface — Neovim, Helix, Sublime LSP, and emacs
  `lsp-mode` consumers can integrate without code changes on our side.
  This hedges against single-editor lock-in.

## Considered Options

### Option A — Embed the LSP in the existing `cli` binary

`shader-clippy --lsp` would put the LSP server behind a flag on the
existing executable. One binary; one release artifact; the rule
registry is already wired up.

- Good: zero new binary; one fewer GitHub Release artifact to track.
- Bad: couples LSP lifetime to CLI-invocation patterns. The CLI is
  designed to start, lint one file, exit. Reshaping `main.cpp` to
  also drive a long-lived JSON-RPC loop blurs the boundary and
  invites corner-case interaction (signals, exit codes, stderr
  coloring) between the two modes.
- Bad: harder to ship to the VS Code Marketplace as a self-contained
  binary — extensions prefer a single-purpose process they can spawn
  and kill cleanly.
- Bad: the CLI is a CI gate; the LSP is an editor server. They have
  different stability contracts. Versioning them independently
  (security fix to the LSP without re-cutting a CLI release) is
  easier when they live in separate binaries.

**Rejected.**

### Option B — Separate `lsp/` target binary, thin wrapper around `core`

A new top-level `lsp/` directory mirroring `cli/`'s shape. New target
`shader_clippy_lsp` (CMake naming convention; produces a binary called
`shader-clippy-lsp`). Built on the same `core` library; uses the same
Slang and tree-sitter dependencies; ships as its own Release artifact;
the VS Code extension launches it as a subprocess.

- Good: clean lifecycle. Extension spawns it, sends `initialize`,
  exchanges JSON-RPC messages, sends `shutdown` + `exit`, kills it.
  Standard LSP shape.
- Good: easy to swap implementations. If a contributor wants to write
  a Rust LSP server that talks to the C++ `core` via a stable C ABI
  someday, the boundary already exists — `lsp/` is the LSP, not the
  rule engine.
- Good: per-editor extensions all spawn the same binary; the binary
  is the contract.
- Good: matches Rust-clippy's shape (`clippy-driver` is a separate
  binary from `cargo clippy`) — established precedent in the
  reputation-comparison space.

**Chosen.**

### Option C — Run the LSP inside the VS Code extension via wasm of `core`

Compile `core` to wasm; load it from the TypeScript extension; the LSP
"server" runs inside the VS Code process.

- Good: no separate binary to download or distribute. Extension is
  fully self-contained.
- Good: avoids cross-platform binary signing entirely.
- Bad: Slang does not ship a wasm build today. Building Slang for
  wasm is a research project of its own and would gate Phase 5 on a
  multi-month upstream effort.
- Bad: large compile target; tree-sitter-hlsl + toml++ + Slang +
  glslang + spirv-tools all transitively wasm-built. The download
  size implications for the extension are substantial.
- Bad: gives up the long-lived-process benefit. The extension host
  already has a process; making the lint engine share it does not
  buy anything we don't already get from a subprocess server.

**Rejected.** Revisit if Slang gains a wasm target post-v0.5.

## Decision Outcome

The architecture chosen is **Option B: a separate `lsp/` target binary
that thinly wraps `core`, with a TypeScript VS Code extension that
launches it as a subprocess**. The architecture is broken into the
seven concrete pieces below.

### 1. `lsp/` C++ binary

A new top-level directory `lsp/` mirroring `cli/`'s shape:

```
lsp/
    src/
        main.cpp              # entry; stdin/stdout JSON-RPC pump
        rpc/
            dispatcher.hpp    # method dispatch, handler registry
            dispatcher.cpp
            framing.hpp       # Content-Length header framing
            framing.cpp
        document/
            manager.hpp       # open-doc registry, incremental edits
            manager.cpp
        capabilities/
            initialize.cpp    # initialize / initialized
            diagnostics.cpp   # publishDiagnostics
            code_actions.cpp  # codeAction → WorkspaceEdit
            hover.cpp         # hover → docs page link
            configuration.cpp # workspace/configuration pull
        CMakeLists.txt
```

CMake target name: `shader_clippy_lsp` (per the `shader_clippy_*` naming
convention used by `shader_clippy_warnings` and the existing `core`
library). Produces a binary called `shader-clippy-lsp` (matching the
`shader-clippy` CLI's hyphenated user-facing name).

Built against the same vendored Slang + tree-sitter + tomlplusplus
submodules as `core`. Links `shader_clippy_core` (PUBLIC) and
`shader_clippy_warnings` (PRIVATE), same as the CLI.

### 2. JSON-RPC layer choice

**Locked: `nlohmann/json` (MIT, header-only) + a thin in-tree JSON-RPC
dispatcher under `lsp/src/rpc/`.**

Considered:

- **`lsp-framework`** (BSL-1.0, header-only, modern C++). Provides
  full LSP types and dispatch. Convenient but adds a heavyweight
  dependency surface for a server that ultimately handles ~10 LSP
  methods at v0.5.
- **`cpp-httplib`'s JSON sublibrary** — wrong shape; HTTP framing,
  not LSP framing.
- **Hand-roll a JSON parser** — wasted effort; the project does not
  have a JSON-parsing competency to develop.
- **`nlohmann/json`** (chosen). MIT, header-only, ubiquitous, compile-time
  cost is well-understood, IDE / Marketplace consumers already know it.
  The thin dispatcher we write on top is ~300–500 LOC of LSP-specific
  framing and method-routing.

Tradeoff: we own the dispatch logic. That is a cost. The benefit is
that the server's surface is exactly the methods we implement — no
unused 3.17 spec surface compiled in, no third-party LSP-framework
versioning hazard riding alongside Slang's already-difficult ABI
surface. Marketplace listing notes the in-tree dispatcher; if it
becomes a maintenance burden post-v0.5, swap to `lsp-framework` in a
focused PR.

Vendor `nlohmann/json` as `external/nlohmann_json/` git submodule,
following the same pattern as `external/tomlplusplus/`. Single-header
include, `#include "nlohmann/json.hpp"` from `lsp/src/rpc/` only.
Add a `cmake/UseNlohmannJson.cmake` helper alongside
`cmake/UseTomlPlusPlus.cmake`. Pin the version in
`cmake/NlohmannJsonVersion.cmake` (mirror the pattern of
`cmake/SlangVersion.cmake`); bump deliberately.

`THIRD_PARTY_LICENSES.md` gains an `nlohmann/json` section per ADR
0006's release-engineering checklist.

### 3. Document lifecycle

A new `DocumentManager` lives in `lsp/src/document/manager.{hpp,cpp}`.
It owns the set of open documents and applies the incremental edits
that LSP `textDocument/didChange` delivers (full-document or range-
based, per the `TextDocumentSyncKind` declared in `initialize`).

Each open document is held as:

```cpp
struct OpenDocument {
    DocumentUri        uri;
    std::filesystem::path path;     // resolved from URI
    std::string        contents;     // canonical UTF-8
    std::int32_t       version = 0;  // LSP doc version
    std::optional<Config> resolved_config;  // walk-up cache
    std::filesystem::path config_path;       // for invalidation
};
```

Incremental edits reuse the existing `core` byte-span machinery — LSP
ships line/character positions, the manager translates those to byte
offsets via `SourceFile::line_text` / line-start tables.

Diagnostics push:

- **`textDocument/didChange`**: schedule a re-lint, debounced ~150 ms
  to avoid burning CPU on every keystroke.
- **`textDocument/didSave`**: re-lint synchronously and push the new
  diagnostics before the response fires.
- **`textDocument/didClose`**: drop the document; clear its
  diagnostics with an empty `publishDiagnostics`.

The lint call is the existing `core` `lint(sources, src_id, rules,
config, path)` — no new entry point. The Slang `IGlobalSession` is a
process-singleton (per ADR 0012); per-document `ISession` reuses the
Phase 3 reflection cache. AST-only runs (no reflection-stage rule
enabled in the resolved config) skip the engine entirely per ADR 0012's
lazy-invocation rule.

### 4. Workspace + config

Per-document config resolution uses the existing `find_config()`
walk-up resolver verbatim. The walk-up is bounded by the first parent
containing `.git/` (workspace boundary) and the filesystem root,
exactly as the CLI behaves today.

LSP-specific additions:

- **`workspace/configuration` pull**. When the client supports it, the
  server can pull `shaderClippy.*` settings from the editor instead of
  resolving from disk. Used for editor-only overrides (target-profile
  override, reflection toggle) that don't live in the project's
  checked-in `.shader-clippy.toml`.
- **`client/registerCapability` for file-watcher**. Server registers a
  watch on `**/.shader-clippy.toml` at `initialize`. On
  `workspace/didChangeWatchedFiles`, the server re-resolves config
  for every open document whose `config_path` matches the changed
  file (or whose walk-up would now resolve differently if the file is
  newly created), and re-lints them.
- **`workspace/didChangeConfiguration`**. Mirror — re-resolve and
  re-lint when the editor signals a settings change.

Multi-root workspaces: each document resolves its own config
independently via walk-up; cross-root state stays decoupled.

### 5. LSP capabilities (initial scope)

Initial scope, in scope for v0.5:

| Method | Purpose |
| --- | --- |
| `initialize` / `initialized` | Capability negotiation; advertise sync, code actions, hover, file-watcher |
| `shutdown` / `exit` | Graceful teardown |
| `textDocument/didOpen` | Register doc, resolve config, lint, push diagnostics |
| `textDocument/didChange` | Apply incremental edit; debounced lint |
| `textDocument/didSave` | Synchronous lint; push diagnostics |
| `textDocument/didClose` | Drop doc, clear diagnostics |
| `textDocument/publishDiagnostics` | Server → client; one per file lint |
| `textDocument/codeAction` | `Fix::edits` → `WorkspaceEdit` (machine-applicable only by default; `quickfix` kind) |
| `textDocument/hover` | Rule id → docs page link + one-line description |
| `workspace/configuration` | Pull editor-only settings on demand |
| `workspace/didChangeConfiguration` | Re-resolve config; re-lint open docs |
| `workspace/didChangeWatchedFiles` | Re-lint on `.shader-clippy.toml` change |
| `client/registerCapability` | Server-side: register the `.shader-clippy.toml` watcher |

**Deferred to Phase 5+ / out of scope for v0.5:**

- `textDocument/signatureHelp` — no signature data without a HLSL
  symbol table; out of scope without dedicated effort.
- `textDocument/definition` / `textDocument/references` — needs a
  symbol table; not in scope.
- `textDocument/completion` — HLSL completions are a separate effort;
  delegate to the Slang Language Service if it ever exposes one.
- `textDocument/formatting` — delegate to clang-format (standalone) or
  Slang's formatter if it gains one. Not our problem.
- `textDocument/semanticTokens` — pretty, but not a developer-loop
  blocker; defer.

The `quickfix` code-action provider maps each `Fix` with
`machine_applicable == true` into a `CodeAction` with `kind ==
"quickfix"`. Suggestion-only fixes (`machine_applicable == false`) are
emitted as `CodeAction` with `kind == "quickfix.suggestion"` and a
clear preview — never silently applied.

### 6. VS Code extension (`vscode-extension/` directory)

A new top-level `vscode-extension/` directory:

```
vscode-extension/
    package.json              # publisher: "nelcit"
    LICENSE                   # Apache-2.0 (copy of repo root)
    README.md                 # Marketplace listing
    src/
        extension.ts          # activation, LSP client wire-up
        server.ts             # binary resolution, download fallback
        config.ts             # settings → server pull-config bridge
    language-configuration.json
    syntaxes/
        # only if the marketplace lacks an HLSL grammar at activation time;
        # else activate on the existing community grammar's language id
    .vscodeignore
    tsconfig.json
    package-lock.json
```

Stack: TypeScript, `vscode-languageclient` v9+ (tracks LSP 3.17), built
with `vsce` (Visual Studio Code Extension manager). License declared
in `package.json` is `"Apache-2.0"` per ADR 0006.

**Activation.** On the `hlsl` language id. Ships its own
`language-configuration.json` (comment markers, brackets, auto-close
pairs) so the extension is functional even if no other HLSL extension
is installed; if a community HLSL grammar is the conventional one,
activation contributes to it rather than replacing it.

**Settings (`shaderClippy.*`):**

| Setting | Type | Default | Purpose |
| --- | --- | --- | --- |
| `shaderClippy.serverPath` | `string` | `""` | Explicit path to `shader-clippy-lsp` binary; empty triggers download/cache resolution |
| `shaderClippy.targetProfile` | `string` | `""` | Forwards to `LintOptions::target_profile` per ADR 0012; empty means per-stage default |
| `shaderClippy.enableReflection` | `boolean` | `true` | Phase 3 toggle — `LintOptions::enable_reflection` |
| `shaderClippy.enableControlFlow` | `boolean` | `true` | Phase 4 toggle (placeholder; ADR for Phase 4 infra cross-references this setting) |
| `shaderClippy.trace.server` | `string` | `"off"` | Standard `vscode-languageclient` trace setting |

**Server-binary resolution.** On activation:

1. If `shaderClippy.serverPath` is set, use it.
2. Else, look for the binary in the extension's persistent storage
   (`extensionContext.globalStorageUri / shader-clippy-lsp/<version>/`).
3. If absent, download from the matching GitHub Release asset for
   the user's `process.platform` × `process.arch`; verify the
   release's published SHA-256; cache it.
4. If the user is offline and no cache exists, surface a Marketplace-
   visible error message with instructions to install the binary
   manually and set `shaderClippy.serverPath`.

The download manifest URL is hard-coded to the `NelCit/shader-clippy`
GitHub Releases endpoint and pinned to the extension's version (so a
v0.5.0 extension downloads a v0.5.0 LSP binary, never trips into a
v0.6 binary mid-session).

Bundled-vs-downloaded decision: download (lazy) for v0.5 — keeps the
extension `.vsix` small enough for a fast Marketplace install. If
download proves fragile in user reports, switch to bundled per-platform
`.vsix` artifacts in v0.6. Marketplace size limits accommodate either
choice.

**Telemetry.** None. This is a developer tool; we do not ship anonymous
usage data without a separate ADR. Marketplace listing explicitly
states "no telemetry" as a feature.

### 7. macOS CI bringup

Phase 5 brings macOS into the CI matrix — finally, per CLAUDE.md
"macOS CI deferred to Phase 5" and per ADR 0005's "macOS deferred until
Phase 5 (existing ROADMAP open question)".

Concrete changes to `.github/workflows/ci.yml`:

- Add `macos-14` (Apple Silicon, default) to the matrix.
- Optionally add `macos-13` (x86_64) for one final cycle, marked
  `continue-on-error: true`, to validate the x86_64 binary path
  before macOS 14 becomes the floor. Drop `macos-13` once Apple's
  GHA-image lifecycle EOLs it.
- Wire the existing 3-tier cache (Slang install-prefix / sccache /
  CMake configure) per ADR 0005 to the new runner. Cache keys gain
  `macos` as a `runner.os` value automatically.
- Slang prebuilt cache: today
  `tools/fetch-slang.ps1` (Windows) and `tools/fetch-slang.sh`
  (Linux) populate the per-user cache. Add `tools/fetch-slang-macos.sh`
  (or extend the existing `tools/fetch-slang.sh` with a
  `macos-aarch64` / `macos-x86_64` branch — implementer's choice).
  Wire up the cache download URLs:
  `slang-<version>-macos-aarch64.tar.gz` and
  `slang-<version>-macos-x86_64.tar.gz`, served from the same Release
  endpoint as the Linux/Windows artifacts.
- macOS Slang build path validation: per CLAUDE.md "macOS Slang/Metal
  paths have been historically rocky", the first PR that adds macOS
  to CI is expected to surface real build failures on the pinned
  Slang version (currently `2026.7.1` per
  `cmake/SlangVersion.cmake`). Mitigation paths in §"Risks &
  mitigations" below.

The `core` linker line on macOS picks up Slang from the cache exactly
the same way as on Linux — `cmake/UseSlang.cmake`'s priority order
already covers all three platforms, only the cache download paths
need wiring.

### 8. Distribution

Two parallel artifact tracks per Release tag:

**GitHub Releases** (per ADR 0005's `softprops/action-gh-release@v2`):

- `shader-clippy-cli-<version>-<os>-<arch>.tar.gz` (existing, extended to
  three OSes).
- `shader-clippy-lsp-<version>-<os>-<arch>.tar.gz` (new).
- Signed where the platform requires it:
  - **macOS**: notarized via `notarytool`. Requires an Apple Developer
    ID; the project obtains one before Phase 5 v0.5 ships, separately
    from this ADR. CI signs in a separate workflow step gated on a
    secret `APPLE_NOTARY_KEY` so PR runs from forks don't fail on
    missing credentials.
  - **Windows**: code signing optional in v0.5 (a self-signed binary
    triggers SmartScreen but does not block install); proper
    Authenticode signing tracked as a v0.6 task once the project has
    funding for an EV certificate.
  - **Linux**: no code signing. SHA-256 sum published alongside the
    Release artifact.

**VS Code Marketplace**:

- Publisher namespace: `nelcit` (lower-case; matches the GitHub org).
  Register early — Marketplace publisher verification can take days.
- Extension identifier: `nelcit.shader-clippy`.
- Publishing CI workflow gated on a release tag (`v*.*.*`): builds the
  `.vsix`, signs with the publisher token from a `VSCE_PAT` secret,
  uploads via `vsce publish`. Workflow is a separate `.github/workflows/
  release-vscode.yml` so a failed Marketplace publish does not block
  the GitHub Release.

Open VSX (the `vscodium` / Eclipse-hosted alternative marketplace) —
defer to v0.6. Same `.vsix`, different publisher token; non-blocking
for v0.5.

## Implementation sub-phases

Mirrors ADR 0009 / 0012's "shared-utilities-PR + parallel-pack
dispatch" pattern, adapted to the LSP shape. Sub-phases 5a and 5b
share design surface and serialise; 5c, 5d, 5e parallelise after 5a.

### Sub-phase 5a — LSP server scaffolding (sequential, must land first)

Single PR, single agent. Lands:

- `lsp/` directory + `shader_clippy_lsp` CMake target.
- `external/nlohmann_json/` git submodule + `cmake/UseNlohmannJson.cmake`.
- `cmake/NlohmannJsonVersion.cmake` pinning the version.
- `lsp/src/rpc/{dispatcher,framing}.{hpp,cpp}` — Content-Length-framed
  JSON-RPC pump, method dispatch.
- `lsp/src/document/manager.{hpp,cpp}` — open-document registry,
  incremental-edit applier.
- `lsp/src/capabilities/{initialize,diagnostics,configuration}.cpp` —
  enough to open a file, lint, push diagnostics, and hold a config
  cache.
- `lsp/src/main.cpp` — stdin/stdout pump, signal handling, exit codes.
- `THIRD_PARTY_LICENSES.md` updated with the nlohmann/json section.
- New unit-test TU `tests/unit/test_lsp_dispatcher.cpp` — feed in a
  recorded LSP transcript, assert the right responses come out.
- Smoke test: a Catch2 driver that opens a one-rule HLSL fixture via a
  mock LSP client, asserts a `publishDiagnostics` arrives with the
  expected rule id.

Effort: ~2 dev weeks. No code actions, no hover, no extension.

### Sub-phase 5b — Code actions (sequential, lands second)

Single PR, single agent. Builds on 5a. Lands:

- `lsp/src/capabilities/code_actions.cpp` — maps `Fix::edits` →
  `WorkspaceEdit::changes[uri]: TextEdit[]`. One CodeAction per Fix.
- `lsp/src/capabilities/hover.cpp` — diagnostic-at-cursor → docs page
  link. Docs URL pattern is hard-coded to
  `https://nelcit.github.io/shader-clippy/rules/<rule-id>/` (matching
  the docs site Phase 6 ships); pre-Phase-6, link to the GitHub
  source path under `docs/rules/<rule-id>.md`.
- Tests: `tests/unit/test_lsp_code_actions.cpp` — recorded transcript
  → assert returned `WorkspaceEdit` matches the `Fix::edits` payload
  byte-for-byte.

Effort: ~3 dev days.

### Sub-phase 5c — VS Code extension (parallel-after-5a)

Single PR, single agent. Can dispatch as soon as 5a lands (does not
need 5b — the extension simply doesn't expose code actions until 5b
ships, but the plumbing works the same). Lands:

- `vscode-extension/` directory.
- `package.json` declaring publisher, license, language id, settings.
- `src/extension.ts`, `src/server.ts`, `src/config.ts`.
- `language-configuration.json`.
- `LICENSE` (Apache-2.0 copy).
- `.vscodeignore`, `tsconfig.json`, `package-lock.json`.
- README + screenshots for the Marketplace listing (placeholder —
  real screenshots come at v0.5 launch).
- Manual smoke checklist in the PR description: open a fixture, see
  squiggles, hit the lightbulb, apply a quick-fix, observe the buffer
  change.

Effort: ~1 dev week.

### Sub-phase 5d — macOS CI bringup (parallel-after-5a)

Single PR, single agent. Independent of 5b/5c. Lands:

- `.github/workflows/ci.yml` matrix gains `macos-14`.
- `tools/fetch-slang-macos.sh` (or extension to existing
  `fetch-slang.sh`).
- Slang prebuilt cache URL convention extended to macOS variants.
- Cache-key updates so the existing 3-tier cache picks up macOS
  cleanly.
- Documentation in `CLAUDE.md` "Slang prebuilt cache (local dev)"
  updated to drop the "macOS path not implemented (Phase 5)" caveat.

Effort: ~3 dev days, modulo Slang-on-macOS surprises (see
"Risks & mitigations" §). Time-box to 5 days; if the Slang macOS
build is broken on the pinned version, ship a "no-reflection macOS
build" stop-gap (LSP compiles without `<slang.h>`-touching code; only
the AST rule packs Phase 0/1/2 work) for v0.5 and roll the full path
into v0.6.

### Sub-phase 5e — Distribution (sequential-last)

Single PR, single agent. Requires 5a + 5b + 5c + 5d to have landed.
Lands:

- `.github/workflows/release.yml` extended:
  - Builds `shader-clippy-lsp` artifact per OS × arch.
  - macOS notarization step gated on `APPLE_NOTARY_KEY` secret.
  - SHA-256 sum file published alongside artifacts.
- New `.github/workflows/release-vscode.yml`:
  - Triggered on the same release tag.
  - Builds the `.vsix` via `vsce package`.
  - Publishes via `vsce publish`, gated on `VSCE_PAT` secret.
  - Open VSX deferred to v0.6 (commented stub left in place).
- `CHANGELOG.md` entry for the v0.5 release.

Effort: ~1 dev week, dominated by the first-time signing / notarization
/ Marketplace-verification roundtrips.

## Consequences

- **LSP becomes the primary developer-facing entry point; the CLI
  remains for CI.** Both surfaces share the same `core` library, the
  same rule registry, the same `.shader-clippy.toml` schema, and the
  same `Fix` / `TextEdit` shape. Diagnostics in VS Code's Problems
  panel match diagnostics in CI logs byte-for-byte (modulo formatting).
- **New JSON dependency on `nlohmann/json` (MIT, header-only).**
  Acceptable per existing third-party policy (Apache / MIT / BSD
  permissive licenses are the policy bar; ADR 0006). One new
  submodule, one new `cmake/Use*.cmake` helper, one new
  `cmake/*Version.cmake` pin, one new section in
  `THIRD_PARTY_LICENSES.md`. Compile-time cost is contained — only
  TUs under `lsp/src/rpc/` include it.
- **Extension lifecycle ties us to VS Code's release cadence.**
  Mitigation: ship a vendor-neutral LSP server that other editors
  can adopt without code changes on our side. Neovim
  (`nvim-lspconfig`), Helix, Sublime LSP, emacs `lsp-mode` users
  configure the binary path and get the full feature surface — that
  is the upside of speaking plain LSP 3.17 instead of a VS-Code-
  proprietary protocol.
- **macOS CI work is finally unblocked.** The Phase 5 ADR makes the
  macOS bringup a hard task with a deadline rather than an open
  ROADMAP question. Reflection rules and IR-level work (Phases 3,
  4, 7) gain macOS coverage as a side effect.
- **VS Code extension users get auto-fix code actions at zero extra
  effort beyond the existing Rewriter.** The Rewriter's `apply()`
  lives in the CLI binary today; in the LSP world, the server emits
  `WorkspaceEdit` from the same `Fix::edits` payload and the
  client (VS Code) applies it — the Rewriter itself is not on the
  hot path for editor-driven fixes.
- **New top-level directories.** `lsp/` and `vscode-extension/` join
  `cli/` and `core/`. ADR 0003 (module decomposition, Proposed) does
  not need to be promoted to Accepted to land them — `lsp/` mirrors
  `cli/`'s shape and `vscode-extension/` is outside the C++ tree.
  Re-visit ADR 0003 separately if/when the directory count exceeds
  what the current layout absorbs cleanly.
- **`<slang.h>` containment continues to hold.** The LSP binary links
  `core` and inherits the same boundary — the CI grep that already
  forbids `<slang.h>` in `core/include/shader_clippy/` keeps passing
  unchanged. ADR 0012 also forbids it under `core/src/rules/`; the
  `lsp/` binary should not include `<slang.h>` either, and the CI
  grep is extended to `lsp/src/`.
- **Single-binary CLI distribution gains a sibling.** The release
  matrix grows from one binary × N OSes to two binaries × N OSes.
  Manageable; the `softprops/action-gh-release@v2` step already
  handles multiple artifacts.

## Risks & mitigations

- **Risk: LSP latency exceeds budget on large shaders.**
  Mitigation: ADR 0012's per-`(SourceId, target_profile)` reflection
  cache amortises N-rule reflection cost to one compile per source
  per profile within one lint run; in the long-lived LSP, that cache
  persists across re-lints, so steady-state is cache-hit. Debounced
  re-lint (~150 ms) on `didChange` smooths burst typing. Hard
  opt-out via `shaderClippy.enableReflection = false` /
  `shaderClippy.enableControlFlow = false` for users on slow machines
  who want only AST rules. Status-bar item shows linting state so
  long-running first-compile is observable rather than mysterious.

- **Risk: `nlohmann/json` adds compile-time cost.**
  Mitigation: header-only is fine; `nlohmann/json` is well-known to
  compile slowly when included in many TUs but is contained to
  `lsp/src/rpc/` here. If compile times grow uncomfortably, precompile
  via a PCH (`lsp/src/rpc/json_pch.hpp`) — no API change. CI sccache
  per ADR 0005 cushions warm-build cost.

- **Risk: macOS Slang path still unstable when we get there.**
  Mitigation: the pinned Slang version (currently `2026.7.1`) is
  validated on Linux + Windows; macOS surfaces are historically the
  rocky ones. Stop-gap path: ship a "no-reflection macOS build" for
  v0.5 — the LSP compiles `core` with `SHADER_CLIPPY_DISABLE_REFLECTION`
  defined, AST rule packs (Phases 0/1/2) work, reflection-stage
  rule packs are silently skipped on macOS until v0.6 reaches full
  feature parity. Accept a documented gap rather than blocking the
  whole release on Slang/macOS plumbing.

- **Risk: VS Code Marketplace gating (publisher verification, EV
  certificate, content review).**
  Mitigation: register the `nelcit` publisher namespace early
  (sub-phase 5a or even before — pre-work). Don't block the v0.5
  GitHub Release on Marketplace approval; the CLI release publishes
  independently of the Marketplace publish. Users installing from
  the GitHub `.vsix` artifact directly is a documented fallback path
  in the Marketplace listing's `README.md`.

## More Information

- **Cross-references**: ADR 0001 (Slang choice — long-lived process
  finally pays back the IGlobalSession warm-up cost), ADR 0005
  (CI/CD — releases via `softprops/action-gh-release@v2`, macOS CI
  unblocked), ADR 0006 (license — Apache-2.0 across CLI, LSP, and
  VS Code extension), ADR 0008 (Phase 1 implementation plan —
  template), ADR 0012 (Phase 3 reflection infrastructure — the
  reflection cache that makes LSP latency budgets achievable),
  ADR 0013 (Phase 4 control-flow infrastructure — the
  `enableControlFlow` setting placeholder defers to it).
- **LSP version targeted**: 3.17 (current at 2026-05-01). Server
  declares `serverInfo.version = SHADER_CLIPPY_VERSION` from
  `core/include/shader_clippy/version.hpp`.
- **Editor-client compatibility**: tested at v0.5 release on VS Code
  ≥ 1.85. Neovim / Helix / Sublime LSP / emacs `lsp-mode`
  compatibility is best-effort — vendor-neutral LSP shape means it
  should work, but we don't run their CI.

## Open question

Should we ship a Neovim plugin alongside the VS Code extension at
Phase 5 v0.5 launch, or defer to community contributions?

The proposal is **defer to community**. The LSP server contract is
the long-term stable surface; once it ships, configuring
`nvim-lspconfig` to spawn `shader-clippy-lsp` is ~10 lines of Lua that
any Neovim user can write themselves. Bundling our own Neovim plugin
implies maintaining it — Neovim plugin conventions, `health.check()`
integration, `:Mason` packaging coordination — and the maintenance
budget is better spent making the server itself good.

If a Neovim community plugin emerges from a community contributor at
v0.5 launch, link to it from the README and bless it as the
recommended Neovim integration. If none emerges and Neovim adoption
turns out to be load-bearing for the project's reputation, revisit
in a v0.6+ ADR — a plugin is much easier to ship after the LSP
surface has proven stable than alongside its first release.
