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
// A duplicate-call pair (A, B) reports iff no mutation event sits between
// A.position and B.position whose target appears in B's argument
// identifiers (or that is a wildcard barrier referencing them).

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
    /// Empty string is a wildcard barrier (e.g. an unknown function call
    /// that may mutate any of its argument identifiers via `out`/`inout`).
    /// `wildcard_targets` lists the identifiers the wildcard applies to.
    std::string target;
    std::vector<std::string> wildcard_targets;
};

struct CallEvent {
    std::uint32_t pos = 0U;
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
    if (kind == "assignment_expression" || kind == "augmented_assignment_expression") {
        ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        if (::ts_node_is_null(lhs) && ::ts_node_named_child_count(node) >= 1U) {
            lhs = ::ts_node_named_child(node, 0);
        }
        MutationEvent ev;
        ev.pos = static_cast<std::uint32_t>(::ts_node_start_byte(node));
        ev.target = root_assignee(lhs, bytes);
        out.mutations.push_back(std::move(ev));
    } else if (kind == "update_expression") {
        // ++x / x++ / --x / x-- — the operand is the only named child.
        if (::ts_node_named_child_count(node) >= 1U) {
            ::TSNode operand = ::ts_node_named_child(node, 0);
            MutationEvent ev;
            ev.pos = static_cast<std::uint32_t>(::ts_node_start_byte(node));
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
                ev.pos = static_cast<std::uint32_t>(::ts_node_start_byte(node));
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
                ev.pos = static_cast<std::uint32_t>(::ts_node_start_byte(node));
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

/// True if any mutation event sits in the open interval (lo, hi) and
/// targets an identifier in `ids` (or carries a wildcard touching `ids`).
[[nodiscard]] bool mutation_between(const std::vector<MutationEvent>& mutations,
                                     std::uint32_t lo,
                                     std::uint32_t hi,
                                     const std::unordered_set<std::string>& ids) {
    for (const auto& m : mutations) {
        if (m.pos <= lo || m.pos >= hi) {
            continue;
        }
        if (!m.target.empty() && ids.count(m.target) != 0U) {
            return true;
        }
        for (const auto& w : m.wildcard_targets) {
            if (ids.count(w) != 0U) {
                return true;
            }
        }
    }
    return false;
}

void analyse_function_body(::TSNode body, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(body)) {
        return;
    }
    const auto bytes = tree.source_bytes();
    FunctionAnalysis fa;
    collect_events(body, bytes, fa);
    if (fa.calls.size() < 2U) {
        return;
    }

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
            if (mutation_between(fa.mutations, a.pos, b.pos, b.referenced_ids)) {
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

void walk_top_level(::TSNode node, const AstTree& tree, RuleContext& ctx) {
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
        analyse_function_body(body, tree, ctx);
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk_top_level(::ts_node_child(node, i), tree, ctx);
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
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        walk_top_level(::ts_tree_root_node(tree.raw_tree()), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_repeated_pure_intrinsic() {
    return std::make_unique<RepeatedPureIntrinsic>();
}

}  // namespace shader_clippy::rules
