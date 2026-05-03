// Public control-flow / data-flow types exposed to rule authors.
//
// This header is the only public surface for the Phase 4 CFG + uniformity
// oracle (ADR 0013). Per the same opacity discipline the reflection header
// follows, no `<tree_sitter/api.h>` or `<slang.h>` types are allowed to leak
// across the public API boundary -- every type defined here is a copyable /
// movable value type. Internal implementation details (`CfgImpl`,
// `UniformityImpl`) are forward-declared and held behind `std::shared_ptr`
// so the engine owns the actual block storage and the public types stay
// cheap to copy.
//
// Rule authors with `stage() == Stage::ControlFlow` receive a
// `const ControlFlowInfo&` in their `on_cfg` hook. They use the helpers
// below to answer questions like "is this `discard` reachable from the
// function entry?" or "is this branch condition wave-uniform?". They never
// see tree-sitter or Slang directly.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "shader_clippy/source.hpp"

namespace shader_clippy {

/// Three-state-plus-one classification of an expression's wave-lane behaviour.
/// Per ADR 0013, the oracle is best-effort: a value tagged `Divergent` may in
/// fact be dynamically uniform at runtime, but the static analysis could not
/// prove it. Rules that act on uniformity do so at warn-severity to absorb
/// the false-positive band.
enum class Uniformity : std::uint8_t {
    Unknown,        ///< Analysis could not classify (e.g. opaque function call).
    Uniform,        ///< Wave-uniform / dynamically uniform across the wave.
    Divergent,      ///< Varies across lanes.
    LoopInvariant,  ///< Uniform-per-iteration; tracked separately for hoist rules.
};

/// PS-only helper-lane tracking. Per ADR 0013 §"Decision Outcome",
/// `PossiblyHelper` means `discard` (or `clip`) is reachable on some path
/// from function entry to this program point; wave intrinsics dispatched
/// from a helper-lane region produce undefined results in PS. Outside PS or
/// before the first reachable `discard`, the state is `NotApplicable`.
enum class HelperLaneState : std::uint8_t {
    NotApplicable,   ///< Not in PS, or pre-discard on this path.
    PossiblyHelper,  ///< PS, post-discard reachable on at least one path.
};

/// Opaque handle into a lint-run-scoped CFG. Cheap to copy. The wrapped
/// value is an integer index into the engine's per-function block table; rule
/// authors should treat it as opaque and only compare it for equality.
class BasicBlockId {
public:
    BasicBlockId() = default;
    explicit constexpr BasicBlockId(std::uint32_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value_;
    }
    [[nodiscard]] friend constexpr bool operator==(BasicBlockId, BasicBlockId) noexcept = default;

private:
    std::uint32_t value_ = 0;
};

/// Per-CFG-node summary, anchored to the underlying AST byte-span. Returned
/// by helpers that walk the CFG and want to expose a uniform façade over
/// "block id + reach-uniformity + helper-lane state at this point".
struct CfgNodeInfo {
    Span span;
    BasicBlockId block;
    Uniformity reach_uniformity = Uniformity::Unknown;
    HelperLaneState helper_lane_state = HelperLaneState::NotApplicable;
};

/// Forward declaration of the engine-internal CFG storage. The header never
/// sees the definition; rule authors hold a `std::shared_ptr<const CfgImpl>`
/// inside `CfgInfo` purely as an opaque keep-alive handle.
struct CfgImpl;

/// Whole-source CFG summary. One `CfgInfo` is built per source per lint run
/// (rather than per function) so that rules can iterate every basic block in
/// every function via `blocks` without juggling a vector-of-vectors. The
/// `entry_span` points at the first function's declaration span as a stable
/// anchor for diagnostics that need to attach at "the top of this CFG".
struct CfgInfo {
    /// Every basic-block id in this source, across every function. The order
    /// is "function-by-function in source order, blocks within each function
    /// in reverse-postorder", but rule authors should not rely on the
    /// specific ordering -- only on the fact that the vector is non-empty
    /// when at least one function was successfully built.
    std::vector<BasicBlockId> blocks;
    /// Best-effort span anchoring "the start of the analysed code". Empty
    /// when no function was built.
    Span entry_span;
    /// Implementation-detail handle into the engine's CFG storage. Held by
    /// `shared_ptr` so the public type stays a value, but opaque to callers.
    std::shared_ptr<const CfgImpl> impl;

    /// True when `to` is reachable from `from` along any path in the CFG.
    /// Returns `false` when either id is invalid for this CFG, when the
    /// blocks belong to different functions, or when the impl handle is null.
    [[nodiscard]] bool reachable_from(BasicBlockId from, BasicBlockId to) const noexcept;

    /// True when `a` dominates `b` in the per-function dominator tree
    /// (every path from the function entry to `b` passes through `a`).
    /// Returns `false` when either id is invalid or when the blocks belong
    /// to different functions.
    [[nodiscard]] bool dominates(BasicBlockId a, BasicBlockId b) const noexcept;

    /// True when every path from `a` to `b` passes through at least one
    /// barrier block (`GroupMemoryBarrier*` / `DeviceMemoryBarrier*`).
    /// Returns `false` when there is at least one barrier-free path, or
    /// when `b` is not reachable from `a`.
    [[nodiscard]] bool barrier_between(BasicBlockId a, BasicBlockId b) const noexcept;
};

/// Forward declaration of the engine-internal uniformity storage.
struct UniformityImpl;

/// Best-effort uniformity oracle keyed by source byte-spans. Rules look up
/// the uniformity of a given expression or branch condition by passing the
/// `Span` recorded by the AST visitor; the oracle returns `Unknown` when no
/// taint information was computed for that span (e.g. the span is outside
/// any analysed function).
struct UniformityOracle {
    /// Implementation-detail handle. Held by `shared_ptr` so the public
    /// type stays a value.
    std::shared_ptr<const UniformityImpl> impl;

    /// Uniformity of the expression covered by `expr`. Returns `Unknown`
    /// when no analysis is available for the span.
    [[nodiscard]] Uniformity of_expr(Span expr) const noexcept;

    /// Uniformity of the branch condition for the `if` / `switch` statement
    /// covered by `branch_stmt`. Returns `Unknown` when the span does not
    /// correspond to a tracked branch.
    [[nodiscard]] Uniformity of_branch(Span branch_stmt) const noexcept;
};

/// Top-level info handed to `Rule::on_cfg`. Aggregates the per-source CFG
/// and the matching uniformity oracle.
struct ControlFlowInfo {
    CfgInfo cfg;
    UniformityOracle uniformity;
};

}  // namespace shader_clippy
