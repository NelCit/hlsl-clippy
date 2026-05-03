// Implementation of the uniformity convenience wrappers declared in
// `uniformity.hpp`. Pure value-type accessors -- no tree-sitter dependency,
// no allocation, no exception path.

#include "rules/util/uniformity.hpp"

#include <string_view>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules::util {

bool is_uniform(const ControlFlowInfo& cfg, Span expr_span) noexcept {
    return cfg.uniformity.of_expr(expr_span) == Uniformity::Uniform;
}

bool is_divergent(const ControlFlowInfo& cfg, Span expr_span) noexcept {
    return cfg.uniformity.of_expr(expr_span) == Uniformity::Divergent;
}

bool is_loop_invariant(const ControlFlowInfo& cfg, Span expr_span) noexcept {
    return cfg.uniformity.of_expr(expr_span) == Uniformity::LoopInvariant;
}

bool divergent_branch(const ControlFlowInfo& cfg, Span branch_stmt_span) noexcept {
    return cfg.uniformity.of_branch(branch_stmt_span) == Uniformity::Divergent;
}

bool is_inherently_divergent_semantic(std::string_view sv_semantic_name) noexcept {
    // Mirrors the seed set in
    // `core/src/control_flow/uniformity_analyzer.cpp::is_divergent_sv_name`.
    // Kept as a public predicate so rules that operate on an SV-semantic
    // string (e.g. extracted from a parameter declarator) can short-circuit
    // without going through a `Span` -> oracle lookup.
    return sv_semantic_name == "SV_DispatchThreadID" || sv_semantic_name == "SV_GroupThreadID" ||
           sv_semantic_name == "SV_GroupIndex" || sv_semantic_name == "SV_VertexID" ||
           sv_semantic_name == "SV_InstanceID" || sv_semantic_name == "SV_PrimitiveID" ||
           sv_semantic_name == "SV_SampleIndex";
}

}  // namespace shader_clippy::rules::util
