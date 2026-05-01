// Internal CFG builder -- walks the tree-sitter AST and emits per-function
// basic blocks + edges. The implementation is the only TU outside
// `core/src/control_flow/` permitted to include `<tree_sitter/api.h>` for
// CFG-construction purposes; all other CFG code talks to the value types in
// `cfg_storage.hpp`.
//
// Per ADR 0013 §"Decision Outcome" point 4, basic-block boundaries are
// introduced at:
//   * function entry
//   * after every branch (`if` / `else` / `switch` / `case`)
//   * loop header / before loop tail (`for` / `while` / `do`)
//   * post-discard / post-clip
//   * post-`GroupMemoryBarrier*` / `DeviceMemoryBarrier*`
//   * function exits (explicit `return`, fall-through to closing brace)
//
// Per ADR 0013 §"Risks & mitigations", the builder tolerates ERROR nodes:
// when a function's tree-sitter subtree contains an ERROR node, the builder
// emits a degenerate single-block CFG for that function and surfaces a
// warn-severity `clippy::cfg-skip` diagnostic so the orchestrator can warn
// the user without crashing or skipping the whole source.

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::control_flow {

/// Result of a single source-level build pass. The CFG storage is owned by
/// the engine; the builder also surfaces any ERROR-node skip diagnostics
/// generated while walking, so the orchestrator can append them to the lint
/// output.
struct BuildResult {
    std::shared_ptr<CfgStorage> storage;
    std::vector<Diagnostic> diagnostics;
};

/// Walk `root` (the source's tree-sitter root node) and emit a `CfgStorage`
/// that contains one function entry per discovered `function_definition`.
/// `source` and `bytes` are forwarded into the storage so that diagnostics
/// can anchor at the right `(SourceId, ByteSpan)` pair.
[[nodiscard]] BuildResult build_cfg(::TSNode root, SourceId source, std::string_view bytes);

}  // namespace hlsl_clippy::control_flow
