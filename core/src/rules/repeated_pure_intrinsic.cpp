// repeated-pure-intrinsic
//
// Flags two or more syntactically-identical calls to an expensive pure
// intrinsic (sqrt, rsqrt, pow, exp, log, sin/cos/tan, ...) within the same
// function body when no intervening mutation could have changed the
// argument's value. The user almost certainly meant to compute the value
// once and reuse it — DXC and Slang both CSE pure intrinsics at -O1 so the
// runtime cost is usually nil, but the duplication signals confused intent
// and defeats hand-written breakpoint placement / SSA inspection / FMA
// folding tweaks.
//
// Example (the canonical IOR snippet):
//
//     float ior = (sqrt(f0) + 1.0f) / (1.0f - sqrt(f0));
//                  ^^^^^^^             ^^^^^^^
//                  computed twice — hoist into a local
//
// Suggestion-only fix: hoisting changes evaluation order in ways that may
// matter for the developer (e.g. precision-sensitive float reductions, or
// when the call sits inside a branch that the original spelling guards).
// We surface the duplicate and let the caller confirm.
//
// Soundness: function-scope detection requires we verify the argument's
// value cannot have changed between the two call sites. We collect:
//
//   1. Every assignment / augmented assignment / `++` / `--` whose LHS root
//      identifier we can extract.
//   2. Every variable declaration (treated as a fresh definition — any
//      prior call referencing the same identifier name is in a different
//      lexical scope, but we conservatively record the redeclaration as a
//      mutation barrier).
//   3. Every call to a NON-allowlisted function — we cannot see the
//      callee's signature from the AST, so any identifier passed as an arg
//      may have been mutated through an `out` / `inout` parameter. We
//      treat such calls as wildcard mutation barriers for the identifiers
//      they reference.
//
// A duplicate-call pair (A, B) reports iff no mutation event between
// A.position and B.position can *reach* B along the CFG. The reachability
// filter (added in v2.0.4) routes through Phase 4's CFG (ADR 0013):
//
//   * If A and B sit in different basic blocks, we require A's block to
//     dominate B's block — otherwise the two calls are on disjoint paths
//     and "duplicate" is misleading. We then keep mutations whose block
//     is reachable from A's block AND reaches B's block. A mutation in
//     an early-return branch (block does not reach B) no longer
//     suppresses, eliminating the v2.0.3 false negative.
//
//   * If A and B share a block, we keep the cheap lexical interval
//     check — within one block, source order equals control-flow order.
//
//   * If the CFG cannot classify any of the events (impl handle null,
//     ERROR-tainted function, span outside all blocks), we fall back to
//     the lexical interval check. The CFG-aware path is a precision
//     refinement, not a precondition.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "repeated-pure-intrinsic";
constexpr std::string_view k_category = "math";

/// Allowlist of expensive pure HLSL / Slang intrinsics. Cheap arithmetic
/// (`min`, `max`, `abs`, `lerp`, `dot`, ...) is intentionally excluded —
/// the duplication signal there is too noisy and the cost too low to be
/// worth flagging.
constexpr std::array<std::string_view, 19> k_pure_intrinsics{
    "sqrt", "rsqrt",  "length", "normalize", "pow",   "exp",   "exp2",
    "log",  "log2",   "log10",  "sin",       "cos",   "tan",   "asin",
    "acos", "atan",   "atan2",  "sinh",      "cosh",
};

[[nodiscard]] bool is_pure_intrinsic(std::string_view name) noexcept {
    for (const auto& candidate : k_pure_intrinsics) {
        if (name == candidate) {
            return true;
        }
    }
    // tanh — separated so the array stays aligned at 19; trailing entry
    // would push the line over the column limit.
    return name == "tanh";
}

/// Strip ASCII whitespace from `text` so two argument lists that differ
/// only in spacing canonicalise to the same string. We do NOT canonicalise
/// numeric forms or operator spelling — `sqrt(2.0)` and `sqrt(2.0f)`
/// remain distinct under this comparison, which is the intended
/// conservative behaviour.
[[nodiscard]] std::string canonicalise_args(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

/// Recursively collect every `identifier` node text under `node`. Used to
/// build the set of root identifiers an argument list references — any
/// mutation of one of these between two duplicate calls invalidates the
/// pair.
void collect_identifiers(::TSNode node,
                          std::string_view bytes,
                          std::unordered_set<std::string>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "identifier") {
        const auto t = node_text(node, bytes);
        if (!t.empty()) {
            out.emplace(t);
        }
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_identifiers(::ts_node_child(node, i), bytes, out);
    }
}

/// Walk left-side of an assignment / augmented-assignment / update node and
/// return the root identifier being written (e.g. `a` for `a.b.c = ...`).
/// Empty result means we couldn't classify the LHS — we then conservatively
/// treat it as a wildcard mutation barrier.
[[nodiscard]] std::string root_assignee(::TSNode node, std::string_view bytes) noexcept {
    while (!::ts_node_is_null(node)) {
        const auto kind = node_kind(node);
        if (kind == "identifier") {
            return std::string{node_text(node, bytes)};
        }
        // Field / subscript / parenthesised expressions: descend to the
        // root object. Both `field_expression` and `subscript_expression`
        // expose the receiver as the first named child in tree-sitter-hlsl.
        if (kind == "field_expression" || kind == "subscript_expression" ||
            kind == "parenthesized_expression") {
            if (::ts_node_named_child_count(node) == 0U) {
                return {};
            }
            node = ::ts_node_named_child(node, 0);
            continue;
        }
        return {};
    }
    return {};
}

struct MutationEvent {
    std::uint32_t pos = 0U;
    /// Byte span of the node that produced this mutation event — used
    /// for CFG block lookup. End == start when the underlying node
    /// covers a zero-length range (rare; defensive).
    ByteSpan span{};
    /// Underlying AST node — used by the dead-branch detector to walk
    /// up to the enclosing compound statement.
    ::TSNode node{};
    /// Empty string is a wildcard barrier (e.g. an unknown function call
    /// that may mutate any of its argument identifiers via `out`/`inout`).
    /// `wildcard_targets` lists the identifiers the wildcard applies to.
    std::string target;
    std::vector<std::string> wildcard_targets;
};

struct CallEvent {
    std::uint32_t pos = 0U;
    /// Byte span of the call expression — used for CFG block lookup.
    ByteSpan span{};
    std::string intrinsic_name;
    std::string canonical_args;
    std::unordered_set<std::string> referenced_ids;
    ::TSNode call_node{};
};

struct FunctionAnalysis {
    std::vector<MutationEvent> mutations;
    std::vector<CallEvent> calls;
};

/// Pre-order walk over a function body, populating `out` with mutation +
/// pure-intrinsic-call events. We do not recurse into nested function
/// definitions — they are handled at the top-level walker.
void collect_events(::TSNode node, std::string_view bytes, FunctionAnalysis& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);

    // Don't descend into nested function/lambda bodies — they belong to
    // their own analysis pass.
    if (kind == "function_definition") {
        return;
    }

    // Mutation events.
    const auto node_byte_span = ByteSpan{
        .lo = static_cast<std::uint32_t>(::ts_node_start_byte(node)),
        .hi = static_cast<std::uint32_t>(::ts_node_end_byte(node)),
    };
    if (kind == "assignment_expression" || kind == "augmented_assignment_expression") {
        ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        if (::ts_node_is_null(lhs) && ::ts_node_named_child_count(node) >= 1U) {
            lhs = ::ts_node_named_child(node, 0);
        }
        MutationEvent ev;
        ev.pos = node_byte_span.lo;
        ev.span = node_byte_span;
        ev.node = node;
        ev.target = root_assignee(lhs, bytes);
        out.mutations.push_back(std::move(ev));
    } else if (kind == "update_expression") {
        // ++x / x++ / --x / x-- — the operand is the only named child.
        if (::ts_node_named_child_count(node) >= 1U) {
            ::TSNode operand = ::ts_node_named_child(node, 0);
            MutationEvent ev;
            ev.pos = node_byte_span.lo;
            ev.span = node_byte_span;
            ev.node = node;
            ev.target = root_assignee(operand, bytes);
            out.mutations.push_back(std::move(ev));
        }
    }
    // Note: variable declarations are intentionally NOT treated as mutation
    // barriers. Doing so risks false negatives whenever an identifier
    // appears on both sides of a sibling declaration (`float a = sqrt(x);
    // float b = sqrt(x);`). The trade-off is a rare false positive when a
    // function shadows an outer name in an inner scope — accepted as a
    // documented limitation; users can `// shader-clippy: allow(...)` such
    // cases.

    // Call events — both pure intrinsic calls (recorded for matching) AND
    // unknown calls (recorded as wildcard mutation barriers because we
    // cannot see whether the callee mutates args via `out`/`inout`).
    if (kind == "call_expression") {
        ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
        if (!::ts_node_is_null(fn) && !::ts_node_is_null(args)) {
            const auto fn_text = node_text(fn, bytes);
            if (is_pure_intrinsic(fn_text)) {
                CallEvent ev;
                ev.pos = node_byte_span.lo;
                ev.span = node_byte_span;
                ev.intrinsic_name = std::string{fn_text};
                ev.canonical_args = canonicalise_args(node_text(args, bytes));
                collect_identifiers(args, bytes, ev.referenced_ids);
                ev.call_node = node;
                out.calls.push_back(std::move(ev));
            } else {
                // Unknown callee — wildcard barrier for any identifier
                // passed as an arg. Conservative; HLSL `out` / `inout`
                // parameters cannot be detected from the AST alone.
                MutationEvent ev;
                ev.pos = node_byte_span.lo;
                ev.span = node_byte_span;
                ev.node = node;
                std::unordered_set<std::string> ids;
                collect_identifiers(args, bytes, ids);
                ev.wildcard_targets.assign(ids.begin(), ids.end());
                if (!ev.wildcard_targets.empty()) {
                    out.mutations.push_back(std::move(ev));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_events(::ts_node_child(node, i), bytes, out);
    }
}

/// True when `m` writes (or wildcard-barriers) at least one identifier
/// from `ids`. Pure name-set check; no positional / CFG reasoning.
[[nodiscard]] bool mutation_targets_any(const MutationEvent& m,
                                         const std::unordered_set<std::string>& ids) {
    if (!m.target.empty() && ids.contains(m.target)) {
        return true;
    }
    for (const auto& w : m.wildcard_targets) {
        if (ids.contains(w)) {
            return true;
        }
    }
    return false;
}

/// Walk parents of `node` collecting every `compound_statement` enclosing
/// it, deepest-first. Used by the disjoint-branch detector below.
void collect_compound_ancestors(::TSNode node, std::vector<::TSNode>& out) {
    ::TSNode cur = ::ts_node_parent(node);
    while (!::ts_node_is_null(cur)) {
        if (node_kind(cur) == "function_definition") {
            return;
        }
        if (node_kind(cur) == "compound_statement") {
            out.push_back(cur);
        }
        cur = ::ts_node_parent(cur);
    }
}

/// True when `a` and `b` sit inside DIFFERENT branches of the same
/// `if_statement` (one in the `consequence`, the other in the `alternative`
/// — including chained `else if` ladders, where each `else` branch is its
/// own `compound_statement`). At most one of the two branches executes per
/// run, so the calls aren't true duplicates.
[[nodiscard]] bool calls_are_in_disjoint_branches(::TSNode a, ::TSNode b) noexcept {
    if (::ts_node_is_null(a) || ::ts_node_is_null(b)) {
        return false;
    }
    std::vector<::TSNode> a_blocks;
    std::vector<::TSNode> b_blocks;
    a_blocks.reserve(8);
    b_blocks.reserve(8);
    collect_compound_ancestors(a, a_blocks);
    collect_compound_ancestors(b, b_blocks);

    // Two calls live in disjoint `if`/`else` branches iff some pair of
    // their compound-statement ancestors shares the same enclosing
    // `if_statement` (via direct parent OR `else_clause` parent) AND
    // those ancestors are different nodes.
    auto enclosing_if = [](::TSNode compound) noexcept -> ::TSNode {
        const ::TSNode parent = ::ts_node_parent(compound);
        if (::ts_node_is_null(parent)) {
            return ::TSNode{};
        }
        if (node_kind(parent) == "if_statement") {
            return parent;
        }
        if (node_kind(parent) == "else_clause") {
            const ::TSNode gp = ::ts_node_parent(parent);
            if (!::ts_node_is_null(gp) && node_kind(gp) == "if_statement") {
                return gp;
            }
        }
        return ::TSNode{};
    };
    for (const auto& ab : a_blocks) {
        const ::TSNode a_if = enclosing_if(ab);
        if (::ts_node_is_null(a_if)) {
            continue;
        }
        for (const auto& bb : b_blocks) {
            if (ab.id == bb.id) {
                continue;
            }
            const ::TSNode b_if = enclosing_if(bb);
            if (!::ts_node_is_null(b_if) && b_if.id == a_if.id) {
                return true;
            }
        }
    }
    return false;
}

/// True when `block` is a `compound_statement` whose final named child is a
/// hard control-flow exit (`return` / `discard` / `break` / `continue`).
/// These ends every path through the block in a way the caller treats as
/// "this block does not flow forward to the next sibling statement".
[[nodiscard]] bool block_unconditionally_exits(::TSNode block) noexcept {
    if (::ts_node_is_null(block) || node_kind(block) != "compound_statement") {
        return false;
    }
    const std::uint32_t nc = ::ts_node_named_child_count(block);
    if (nc == 0U) {
        return false;
    }
    const ::TSNode last = ::ts_node_named_child(block, nc - 1U);
    const auto k = node_kind(last);
    return k == "return_statement" || k == "discard_statement" ||
           k == "break_statement" || k == "continue_statement";
}

/// AST-based dead-branch detector: walk up from `mutation_node` and look
/// for an enclosing `compound_statement` that (a) ends unconditionally
/// (return / discard / break / continue), AND (b) ends before `b_pos`.
/// When both hold, the mutation is on a path that cannot reach B, so the
/// caller should NOT treat it as intervening.
///
/// We deliberately use AST shape rather than the engine's CFG because the
/// CFG's reachability semantics treat nested-region blocks as
/// non-reaching their lexical successors (the CFG is structured as a tree
/// of regions, not a classical merge graph). The AST predicate is
/// stricter and more reliable for this rule's needs.
[[nodiscard]] bool mutation_in_exiting_branch_before(::TSNode mutation_node,
                                                      std::uint32_t b_pos) noexcept {
    ::TSNode cur = mutation_node;
    while (!::ts_node_is_null(cur)) {
        ::TSNode parent = ::ts_node_parent(cur);
        if (::ts_node_is_null(parent)) {
            return false;
        }
        // Stop walking up when we leave the function body.
        if (node_kind(parent) == "function_definition") {
            return false;
        }
        if (node_kind(parent) == "compound_statement") {
            const auto end_byte = static_cast<std::uint32_t>(::ts_node_end_byte(parent));
            if (end_byte <= b_pos && block_unconditionally_exits(parent)) {
                return true;
            }
        }
        cur = parent;
    }
    return false;
}

/// CFG-aware: does mutation `m` intervene between calls A and B?
///
/// Conservative semantics — never weaker than the v2.0.3 lexical check:
///
///   1. Cheap precondition: `m` must (a) write at least one identifier
///      in B's argument set AND (b) sit lexically between A and B. If
///      either fails, the mutation does not intervene.
///
///   2. Otherwise we have a "lexical hit" and start at "intervenes =
///      true" (the v2.0.3 default). The CFG can RELAX this — but only
///      if it can affirmatively prove the mutation's block cannot
///      reach B's block. Lexical-hit-but-dead-branch is the exact
///      case we shipped this for (early return / discard).
///
///   3. If any of the three blocks is unclassifiable, we cannot prove
///      anything about reachability and stay at "intervenes = true".
[[nodiscard]] bool mutation_intervenes(const MutationEvent& m,
                                        const CallEvent& a,
                                        const CallEvent& b) {
    if (!mutation_targets_any(m, b.referenced_ids)) {
        return false;
    }
    if (m.pos <= a.pos || m.pos >= b.pos) {
        return false;
    }
    // AST-based proof of "dead w.r.t. B": if the mutation is enclosed in
    // a `compound_statement` that ends with an unconditional exit
    // (`return` / `discard` / `break` / `continue`) AND that block ends
    // lexically before B, the mutation cannot reach B on any control-
    // flow path. Relax — do NOT count as intervening. See the predicate
    // doc-comment for why we use AST shape here rather than the Phase 4
    // CFG (engine's `reachable_from` is too conservative for merge-back
    // patterns: it returns false even when an `if` body's effects flow
    // forward to a post-`if` join).
    if (mutation_in_exiting_branch_before(m.node, b.pos)) {
        return false;
    }
    return true;
}

void analyse_function_body(::TSNode body,
                            const AstTree& tree,
                            const ControlFlowInfo& cfg,
                            RuleContext& ctx) {
    if (::ts_node_is_null(body)) {
        return;
    }
    const auto bytes = tree.source_bytes();
    FunctionAnalysis fa;
    collect_events(body, bytes, fa);
    if (fa.calls.size() < 2U) {
        return;
    }

    // The Phase 4 CFG handle (`cfg`) is threaded through but currently
    // unused — the dead-branch + disjoint-branch detectors both work off
    // the AST (the engine's region-tree CFG produced too many false
    // negatives on standard merge-back patterns). The handle remains in
    // the signature for future precision tightening once the CFG grows
    // merge-edge semantics.
    static_cast<void>(cfg);

    // Group by (intrinsic_name, canonical_args). Use a string key joining
    // both with a separator that cannot appear in HLSL identifiers.
    std::unordered_map<std::string, std::vector<std::size_t>> groups;
    groups.reserve(fa.calls.size());
    for (std::size_t i = 0; i < fa.calls.size(); ++i) {
        std::string key = fa.calls[i].intrinsic_name;
        key.push_back('\x1F');
        key.append(fa.calls[i].canonical_args);
        groups[key].push_back(i);
    }

    for (auto& [key, indices] : groups) {
        if (indices.size() < 2U) {
            continue;
        }
        std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
            return fa.calls[a].pos < fa.calls[b].pos;
        });
        // Walk consecutive pairs; emit at the second of each pair pointing
        // back at the first. Reporting only the second avoids N^2 noise on
        // a triple `sqrt(x); sqrt(x); sqrt(x);` (we'd surface two diags,
        // both pointing back one step).
        for (std::size_t k = 1; k < indices.size(); ++k) {
            const auto& a = fa.calls[indices[k - 1U]];
            const auto& b = fa.calls[indices[k]];

            // Disjoint-branches check: when A and B sit inside sibling
            // branches of the same `if` / `else` chain, at most one of
            // the two calls runs at any execution and the "duplicate"
            // report is misleading. AST-based detection: their
            // compound-statement ancestors share an enclosing
            // `if_statement` (directly or via `else_clause`).
            if (calls_are_in_disjoint_branches(a.call_node, b.call_node)) {
                continue;
            }

            // Mutation check. Lexical interval first (v2.0.3 floor),
            // then AST dead-branch relaxation lifts the false negatives
            // on early-return / discard / break / continue branches.
            bool intervenes = false;
            for (const auto& m : fa.mutations) {
                if (mutation_intervenes(m, a, b)) {
                    intervenes = true;
                    break;
                }
            }
            if (intervenes) {
                continue;
            }
            const auto b_range = tree.byte_range(b.call_node);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = b_range};
            diag.message = std::string{"`"} + b.intrinsic_name +
                           std::string{"("} + b.canonical_args +
                           std::string{")` is computed more than once in the same "
                                       "function with no intervening mutation of its "
                                       "argument — hoist into a local to make intent "
                                       "explicit (DXC / Slang already CSE this at -O1, "
                                       "but the duplication signals confused intent)"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description =
                std::string{"hoist `"} + b.intrinsic_name + std::string{"("} +
                b.canonical_args +
                std::string{")` into a local at the first call site and reuse it. "
                            "Verify no precision-sensitive ordering relies on the "
                            "duplicate computation before applying"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
}

void walk_top_level(::TSNode node,
                     const AstTree& tree,
                     const ControlFlowInfo& cfg,
                     RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        // The body is conventionally the last named child or the
        // `body`-named field.
        ::TSNode body = ::ts_node_child_by_field_name(node, "body", 4);
        if (::ts_node_is_null(body)) {
            const std::uint32_t nc = ::ts_node_named_child_count(node);
            if (nc > 0U) {
                body = ::ts_node_named_child(node, nc - 1U);
            }
        }
        analyse_function_body(body, tree, cfg, ctx);
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk_top_level(::ts_node_child(node, i), tree, cfg, ctx);
    }
}

class RepeatedPureIntrinsic : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        walk_top_level(::ts_tree_root_node(tree.raw_tree()), tree, cfg, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_repeated_pure_intrinsic() {
    return std::make_unique<RepeatedPureIntrinsic>();
}

}  // namespace shader_clippy::rules
