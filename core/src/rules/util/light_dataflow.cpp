// Implementation of the light data-flow helpers declared in
// `light_dataflow.hpp`. Pure value-type accessors -- no tree-sitter
// dependency, no allocation, no exception path.
//
// `groupshared_read_before_write` and `dead_store` are forward-compatible
// stubs: sub-phase 4a's CFG infrastructure does not yet track per-cell
// access kind or per-variable use-def chains. The API is locked so Phase 4
// rule packs can compile against it today; the implementation tightens
// as the engine grows. See the corresponding header comments for the
// full contract.

#include "rules/util/light_dataflow.hpp"

#include "rules/util/cfg_query.hpp"
#include "rules/util/uniformity.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules::util {

bool groupshared_read_before_write(const ControlFlowInfo& cfg, Span gs_decl_span) noexcept {
    // Forward-compatible stub: see header. We at least exercise the
    // span-locator so the helper returns `false` for spans outside the
    // CFG (rather than surfacing a stale answer) -- this also keeps the
    // smoke test in `test_cfg_util.cpp` honest by not pretending to know
    // an answer we have not computed.
    (void)block_for(cfg, gs_decl_span);
    return false;
}

bool dead_store(const ControlFlowInfo& cfg, Span var_span) noexcept {
    // Forward-compatible stub: see header. Same shape as
    // `groupshared_read_before_write` -- we touch the CFG so rules
    // pass the `Span` through but receive the conservative answer.
    (void)block_for(cfg, var_span);
    return false;
}

bool loop_invariant_expr(const ControlFlowInfo& cfg, Span expr_span) noexcept {
    // The oracle's `LoopInvariant` lattice value encodes "uniform-per-
    // iteration relative to the enclosing loop"; `Uniform` is strictly
    // stronger (uniform across the wave AND the loop iteration). Either
    // qualifies as loop-invariant for hoist-style rules.
    return is_uniform(cfg, expr_span) || is_loop_invariant(cfg, expr_span);
}

}  // namespace shader_clippy::rules::util
