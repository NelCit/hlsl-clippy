// Uniformity analyzer -- AST-level taint propagation. See header for the
// seed list and lattice; this TU implements a single recursive walk that
// classifies each named expression node and stores the result in the
// `expr_uniformity` / `branch_uniformity` tables on `UniformityImplData`.
//
// The classifier intentionally keeps the lattice small (4 states) and the
// transfer functions intentionally simple (`join` is the lub over
// `Unknown ⊑ Uniform | LoopInvariant ⊑ Divergent`). A more precise SSA-style
// pass is planned for the optional Slang-IR refinement layer (ADR 0013
// Option C, deferred).

#include "control_flow/uniformity_analyzer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::control_flow {

namespace {

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* type = ::ts_node_type(node);
    return type != nullptr ? std::string_view{type} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::uint64_t pack_span_bytes(std::uint32_t lo, std::uint32_t hi) noexcept {
    return (static_cast<std::uint64_t>(lo) << 32U) | static_cast<std::uint64_t>(hi);
}

[[nodiscard]] std::uint64_t pack_node(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return 0U;
    }
    return pack_span_bytes(static_cast<std::uint32_t>(::ts_node_start_byte(node)),
                           static_cast<std::uint32_t>(::ts_node_end_byte(node)));
}

/// HLSL system-value semantics tags that are inherently divergent. We
/// match by suffix/contains because the AST surface for semantic tags is
/// `: SV_XXX` -- the colon-prefixed token shows up either as part of a
/// `semantics` node or inside a parameter declarator's text.
[[nodiscard]] bool is_divergent_sv_name(std::string_view ident) noexcept {
    return ident == "SV_DispatchThreadID" || ident == "SV_GroupThreadID" ||
           ident == "SV_GroupIndex" || ident == "SV_VertexID" || ident == "SV_InstanceID" ||
           ident == "SV_PrimitiveID" || ident == "SV_SampleIndex";
}

/// Wave-lane intrinsics that return per-lane (divergent) values. Distinct
/// from `WaveGetLaneCount()`, which is wave-uniform.
[[nodiscard]] bool is_divergent_wave_intrinsic(std::string_view ident) noexcept {
    return ident == "WaveGetLaneIndex" || ident == "WaveReadLaneFirst" ||
           ident == "WaveActiveAllEqual" || ident == "WaveActiveAnyTrue" ||
           ident == "WaveActiveBallot";
}

/// Wave intrinsics that return wave-uniform values.
[[nodiscard]] bool is_uniform_wave_intrinsic(std::string_view ident) noexcept {
    return ident == "WaveGetLaneCount" || ident == "WaveIsFirstLane";
}

/// Uniformity meet (least-upper-bound) over the lattice
/// `Unknown ⊑ Uniform | LoopInvariant ⊑ Divergent`. `Divergent` wins,
/// `Unknown` loses, mixed Uniform+LoopInvariant -> LoopInvariant.
[[nodiscard]] Uniformity join(Uniformity lhs, Uniformity rhs) noexcept {
    if (lhs == Uniformity::Divergent || rhs == Uniformity::Divergent) {
        return Uniformity::Divergent;
    }
    if (lhs == Uniformity::Unknown) {
        return rhs;
    }
    if (rhs == Uniformity::Unknown) {
        return lhs;
    }
    if (lhs == Uniformity::LoopInvariant || rhs == Uniformity::LoopInvariant) {
        return Uniformity::LoopInvariant;
    }
    return Uniformity::Uniform;
}

/// Per-source analyzer state. Holds the divergent-identifier set built
/// from system-value parameter scanning and the output tables.
struct Analyzer {
    SourceId source;
    std::string_view bytes;
    UniformityImplData* out = nullptr;
    /// Identifier names (parameter declarations) that carry a divergent
    /// system-value semantic. Populated by `seed_divergent_parameters`.
    std::unordered_set<std::string> divergent_idents;
    /// Resource bindings flagged by reflection as NonUniform. Populated
    /// only when reflection is available.
    std::unordered_set<std::string> divergent_resources;
    std::uint32_t inlining_depth = 3U;

    /// Walk every parameter_declaration in the source, find the ones
    /// tagged with a divergent SV_ semantic, and stash their identifier
    /// names in `divergent_idents`.
    void seed_divergent_parameters(::TSNode root) {
        if (::ts_node_is_null(root)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            const auto kind = node_kind(node);
            if (kind == "parameter_declaration" || kind == "parameter") {
                const auto text = node_text(node, bytes);
                // Cheap text scan -- the parameter declaration carries
                // its identifier and its semantic tag in one fragment of
                // source. We extract any `SV_XXX` token and the
                // identifier that precedes the semantic colon.
                bool divergent_sv = false;
                for (const auto sv : {std::string_view{"SV_DispatchThreadID"},
                                      std::string_view{"SV_GroupThreadID"},
                                      std::string_view{"SV_GroupIndex"},
                                      std::string_view{"SV_VertexID"},
                                      std::string_view{"SV_InstanceID"},
                                      std::string_view{"SV_PrimitiveID"},
                                      std::string_view{"SV_SampleIndex"}}) {
                    if (text.find(sv) != std::string_view::npos) {
                        divergent_sv = true;
                        break;
                    }
                }
                if (divergent_sv) {
                    // Grab the identifier child so we can taint by name.
                    extract_param_idents(node, divergent_idents);
                }
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child)) {
                    stack.push_back(child);
                }
            }
        }
    }

    /// Collect every identifier text inside a parameter_declaration. We
    /// can't do better than "any identifier" cheaply -- the grammar's
    /// identifier child may be unnamed, and the declarator field may be
    /// absent on the partial-grammar paths.
    void extract_param_idents(::TSNode param, std::unordered_set<std::string>& out_set) {
        if (::ts_node_is_null(param)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(param);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            if (node_kind(node) == "identifier") {
                const auto text = node_text(node, bytes);
                // Skip the SV_ tokens themselves -- they carry the same
                // node kind as user identifiers but we don't want to
                // taint by them.
                if (!text.empty() && !text.starts_with("SV_") && !text.starts_with("TEXCOORD") &&
                    text != "POSITION") {
                    out_set.emplace(std::string{text});
                }
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child)) {
                    stack.push_back(child);
                }
            }
        }
    }

    /// Classify one expression node. Recursive walk down the operand tree;
    /// each subexpression's classification is stored in `expr_uniformity`
    /// and the parent's classification is the join over its operands.
    [[nodiscard]] Uniformity classify(::TSNode node) {
        if (::ts_node_is_null(node) || !::ts_node_is_named(node)) {
            return Uniformity::Unknown;
        }
        const auto kind = node_kind(node);

        // Literals are always uniform.
        if (kind == "number_literal" || kind == "string_literal" || kind == "true" ||
            kind == "false") {
            const auto u = Uniformity::Uniform;
            out->expr_uniformity[pack_node(node)] = u;
            return u;
        }

        // Identifier: divergent if it names a divergent SV-tagged param,
        // a NonUniform resource, or a divergent wave intrinsic call. Else
        // uniform-by-default (cbuffers / locals are uniform until a
        // divergent value is assigned to them; we don't track that here).
        if (kind == "identifier") {
            const auto text = node_text(node, bytes);
            const auto str = std::string{text};
            Uniformity u = Uniformity::Uniform;
            if (divergent_idents.contains(str)) {
                u = Uniformity::Divergent;
            } else if (divergent_resources.contains(str)) {
                u = Uniformity::Divergent;
            } else if (is_divergent_sv_name(text) || is_divergent_wave_intrinsic(text)) {
                u = Uniformity::Divergent;
            } else if (is_uniform_wave_intrinsic(text)) {
                u = Uniformity::Uniform;
            }
            out->expr_uniformity[pack_node(node)] = u;
            return u;
        }

        // Call expression: classify the callee + each argument; if the
        // callee is a known-divergent intrinsic, the call result is
        // divergent regardless of arguments.
        if (kind == "call_expression") {
            const ::TSNode callee = ::ts_node_child_by_field_name(node, "function", 8U);
            const auto callee_text = node_text(callee, bytes);
            if (is_divergent_wave_intrinsic(callee_text) || is_divergent_sv_name(callee_text)) {
                out->expr_uniformity[pack_node(node)] = Uniformity::Divergent;
                return Uniformity::Divergent;
            }
            if (is_uniform_wave_intrinsic(callee_text)) {
                out->expr_uniformity[pack_node(node)] = Uniformity::Uniform;
                return Uniformity::Uniform;
            }
            // Otherwise: lub over arguments. Don't recurse into the
            // callee subtree (it's typically just an identifier already
            // classified above).
            Uniformity u = Uniformity::Unknown;
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9U);
            if (!::ts_node_is_null(args)) {
                const std::uint32_t cnt = ::ts_node_child_count(args);
                for (std::uint32_t i = 0; i < cnt; ++i) {
                    const auto child = ::ts_node_child(args, i);
                    if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                        u = join(u, classify(child));
                    }
                }
            }
            // Inter-procedural inlining: when depth allows, we'd descend
            // into the callee's body here. For sub-phase 4a we stop at
            // depth 0 and treat user functions as opaque; this matches
            // the "best-effort" contract in ADR 0013.
            (void)inlining_depth;
            out->expr_uniformity[pack_node(node)] = u;
            return u;
        }

        // Subscript / field-access: uniformity of the base joined with
        // uniformity of the index (subscript).
        if (kind == "subscript_expression" || kind == "field_expression" ||
            kind == "binary_expression" || kind == "unary_expression" ||
            kind == "parenthesized_expression" || kind == "conditional_expression" ||
            kind == "assignment_expression" || kind == "comma_expression" ||
            kind == "update_expression" || kind == "cast_expression") {
            Uniformity u = Uniformity::Unknown;
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                    u = join(u, classify(child));
                }
            }
            out->expr_uniformity[pack_node(node)] = u;
            return u;
        }

        // Default: walk children, lub the results.
        Uniformity u = Uniformity::Unknown;
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const auto child = ::ts_node_child(node, i);
            if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                u = join(u, classify(child));
            }
        }
        out->expr_uniformity[pack_node(node)] = u;
        return u;
    }

    /// Walk the source looking for `if_statement` / `switch_statement`
    /// nodes. For each, classify the condition expression and stamp the
    /// branch's full span in `branch_uniformity`. We use the full
    /// statement span (not just the condition span) because rules query
    /// "uniformity of this branch statement", consistent with the public
    /// API contract on `UniformityOracle::of_branch`.
    void scan_branches(::TSNode root) {
        if (::ts_node_is_null(root)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            const auto kind = node_kind(node);
            if (kind == "if_statement" || kind == "switch_statement") {
                ::TSNode cond = ::ts_node_child_by_field_name(node, "condition", 9U);
                if (::ts_node_is_null(cond)) {
                    // Fallback: first named child after the keyword.
                    const std::uint32_t cnt = ::ts_node_child_count(node);
                    for (std::uint32_t i = 0; i < cnt; ++i) {
                        const auto child = ::ts_node_child(node, i);
                        if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                            cond = child;
                            break;
                        }
                    }
                }
                Uniformity u = Uniformity::Unknown;
                if (!::ts_node_is_null(cond)) {
                    u = classify(cond);
                }
                out->branch_uniformity[pack_node(node)] = u;
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child)) {
                    stack.push_back(child);
                }
            }
        }
    }
};

}  // namespace

void analyse_uniformity(::TSNode root,
                        SourceId source,
                        std::string_view bytes,
                        const ReflectionInfo* reflection,
                        std::uint32_t cfg_inlining_depth,
                        UniformityImplData& out) {
    Analyzer a;
    a.source = source;
    a.bytes = bytes;
    a.out = &out;
    a.inlining_depth = cfg_inlining_depth;

    if (reflection != nullptr) {
        // ResourceBinding doesn't carry a NonUniform tag in the ADR 0012
        // surface, so the resource-divergence channel stays empty for
        // sub-phase 4a. When the reflection helper for NonUniform lands,
        // populate `a.divergent_resources` here.
        (void)reflection;
    }

    a.seed_divergent_parameters(root);

    // Classify every expression in the source. Doing one full walk lets
    // every queried span land in the table; the cost is O(N) over named
    // nodes, dominated by the AST size, not by the rule count.
    if (!::ts_node_is_null(root)) {
        std::vector<::TSNode> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            if (::ts_node_is_named(node)) {
                (void)a.classify(node);
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child)) {
                    stack.push_back(child);
                }
            }
        }
    }

    a.scan_branches(root);
}

}  // namespace hlsl_clippy::control_flow
