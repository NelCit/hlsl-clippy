// Liveness analysis implementation -- ADR 0017 sub-phase 7b.1.
//
// We re-walk the parsed tree-sitter root and bucket every named node into
// the basic block whose recorded span tightly encloses the node's span.
// The buckets feed two passes:
//
//   1) Per-block def / use extraction (single AST walk).
//   2) Standard backward-dataflow fixed-point iteration over the CFG.
//
// Variable identity is by name only -- this matches how the existing
// uniformity oracle (`uniformity_analyzer.cpp`) treats identifiers, and
// suffices for the Phase 7 rules consuming this analysis (live state
// across `TraceRay`, dead-store detection, AST-level register pressure
// heuristic). Scope-aware shadowing and SSA-style precision are deferred
// to the optional Slang-IR refinement layer per ADR 0013 Option C.
//
// Call boundaries are conservative: we treat any non-recognised call as
// a full kill / full re-gen point (i.e. the call's argument identifiers
// count as uses, but no defs are introduced by the call itself, leaving
// in-flight locals live across the boundary). That is what the test
// fixture for "liveness across a function call" asserts.

#include "rules/util/liveness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::util {

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

/// HLSL keywords / built-in type tokens that must never be classified as
/// variable identifiers. The grammar surfaces these as `identifier` nodes
/// in places (e.g. type names appearing in declarations); the liveness
/// pass would otherwise misread them as uses of a local. We err on the
/// side of pruning common type lexemes; locals that legitimately share a
/// name with a type token are vanishingly rare in practice.
[[nodiscard]] bool is_reserved_or_type_token(std::string_view text) noexcept {
    if (text.empty()) {
        return true;
    }
    // Numeric scalar / vector / matrix shapes -- we only check the prefix
    // because tree-sitter-hlsl exposes vector shapes as `floatN`, `intN`,
    // `uintN` etc. We use a tight prefix list to avoid overfiring.
    static constexpr std::array<std::string_view, 19> k_type_prefixes = {
        "float",     "double",  "half",    "min10float", "min16float", "min12int", "min16int",
        "min16uint", "uint",    "int",     "bool",       "uint16_t",   "uint32_t", "uint64_t",
        "int16_t",   "int32_t", "int64_t", "vector",     "matrix",
    };
    for (const auto pfx : k_type_prefixes) {
        if (text == pfx) {
            return true;
        }
        // Allow exact-or-prefix-with-digit so `float`, `float2`, `float4x4`
        // are all reserved, but a user identifier like `floats` is not.
        if (text.starts_with(pfx) && text.size() > pfx.size()) {
            const char tail = text[pfx.size()];
            if (tail >= '0' && tail <= '9') {
                return true;
            }
        }
    }
    static constexpr std::array<std::string_view, 43> k_keywords = {
        "if",
        "else",
        "for",
        "while",
        "do",
        "switch",
        "case",
        "default",
        "break",
        "continue",
        "return",
        "discard",
        "true",
        "false",
        "void",
        "in",
        "out",
        "inout",
        "const",
        "static",
        "uniform",
        "groupshared",
        "shared",
        "register",
        "struct",
        "cbuffer",
        "tbuffer",
        "typedef",
        "namespace",
        "this",
        "nullptr",
        "centroid",
        "noperspective",
        "linear",
        "sample",
        "nointerpolation",
        "precise",
        "snorm",
        "unorm",
        "row_major",
        "column_major",
        "globallycoherent",
        "reordercoherent",
    };
    for (const auto kw : k_keywords) {
        if (text == kw) {
            return true;
        }
    }
    return false;
}

/// Pack `(byte_lo, byte_hi)` into a 64-bit key for fast equality lookup.
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

/// Locate the `BasicBlockId` whose recorded span most-tightly encloses
/// the AST node. Returns 0 (invalid) when no enclosing block exists for
/// the node's offset (e.g. the node is in a function the CFG skipped).
[[nodiscard]] std::uint32_t find_enclosing_block(const control_flow::CfgStorage& storage,
                                                 std::uint32_t node_lo,
                                                 std::uint32_t node_hi) noexcept {
    std::uint32_t best = 0U;
    std::uint32_t best_size = 0xFFFFFFFFU;
    for (const auto& [block_span, raw] : storage.span_to_block) {
        if (block_span.bytes.lo > node_lo || block_span.bytes.hi < node_hi) {
            continue;
        }
        const std::uint32_t sz = block_span.bytes.hi - block_span.bytes.lo;
        if (sz < best_size) {
            best_size = sz;
            best = raw;
        }
    }
    return best;
}

/// Locate the n-th `BasicBlockId` whose recorded span exactly equals the
/// given range. Used to disambiguate the loop-{header,body,exit} triple
/// the CFG builder allocates with identical spans -- they are recorded
/// in `span_to_block` in allocation order (header, body, exit), so
/// passing `n = 0` returns the header, `n = 1` returns the body, `n = 2`
/// returns the exit. Returns 0 when fewer than `n + 1` matches exist.
[[nodiscard]] std::uint32_t find_nth_block_with_exact_span(const control_flow::CfgStorage& storage,
                                                           std::uint32_t node_lo,
                                                           std::uint32_t node_hi,
                                                           std::uint32_t n) noexcept {
    std::uint32_t seen = 0U;
    for (const auto& [block_span, raw] : storage.span_to_block) {
        if (block_span.bytes.lo == node_lo && block_span.bytes.hi == node_hi) {
            if (seen == n) {
                return raw;
            }
            ++seen;
        }
    }
    return 0U;
}

/// Per-block `use` / `def` sets, indexed by raw `BasicBlockId`. We use a
/// `std::unordered_set<std::string>` per block for O(1) "already seen"
/// checks during AST walk, then sort+dedupe at the end before exposing
/// the final `LivenessInfo`.
struct DefUseTables {
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> use_per_block;
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> def_per_block;
};

/// Walker state. `defs_seen` tracks which identifier names have already
/// been bound by a def in the *currently traversed* block. A use of a name
/// after a def in the same block is killed by that def -- we only record
/// the use if no preceding def exists in the same block (this is the
/// standard `use[B]` set semantics: identifiers used before any def in
/// `B`).
struct Walker {
    const control_flow::CfgStorage* storage = nullptr;
    std::string_view bytes;
    DefUseTables* tables = nullptr;
    /// Per-(block, name) "have we already recorded a def?" memo. Used to
    /// suppress add-as-use when a prior def in the same block exists.
    std::unordered_map<std::uint64_t, bool> defs_seen;
    /// Set of node identity (packed span) tags that should be skipped by
    /// the use-classification pass because they were already processed
    /// elsewhere as a def or as a callee identifier. Without this, the
    /// LHS identifier of `x = ...` would also count as a use.
    std::unordered_set<std::uint64_t> use_skip;

    [[nodiscard]] std::uint64_t def_key(std::uint32_t block, std::string_view name) const noexcept {
        // FNV-1a fold of the name + block id. The key is `(block, name)`
        // -- a def of name N in block B does NOT suppress a use of N in
        // block C != B. This is the standard `use[B]` semantics.
        std::uint64_t h = static_cast<std::uint64_t>(block) * 1099511628211ULL;
        for (const char c : name) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        return h;
    }

    [[nodiscard]] std::uint32_t block_of(::TSNode node) const noexcept {
        if (::ts_node_is_null(node)) {
            return 0U;
        }
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
        return find_enclosing_block(*storage, lo, hi);
    }

    void record_def(::TSNode ident, std::uint32_t block) {
        if (block == 0U) {
            return;
        }
        const auto text = node_text(ident, bytes);
        if (text.empty() || is_reserved_or_type_token(text)) {
            return;
        }
        const std::string name{text};
        tables->def_per_block[block].insert(name);
        defs_seen[def_key(block, text)] = true;
        use_skip.insert(pack_node(ident));
    }

    void record_use(::TSNode ident, std::uint32_t block) {
        if (block == 0U) {
            return;
        }
        if (use_skip.contains(pack_node(ident))) {
            return;
        }
        const auto text = node_text(ident, bytes);
        if (text.empty() || is_reserved_or_type_token(text)) {
            return;
        }
        // Suppress use if a def in this block already bound the name --
        // the canonical `use[B]` set is "names used before any def in B".
        const auto key = def_key(block, text);
        const auto it = defs_seen.find(key);
        if (it != defs_seen.end() && it->second) {
            return;
        }
        tables->use_per_block[block].insert(std::string{text});
    }

    /// Walk the LHS of an assignment_expression. The leftmost identifier
    /// is a def; any indices inside subscript expressions become uses.
    void walk_assignment_lhs(::TSNode lhs, std::uint32_t block) {
        if (::ts_node_is_null(lhs)) {
            return;
        }
        const auto kind = node_kind(lhs);
        if (kind == "identifier") {
            record_def(lhs, block);
            return;
        }
        if (kind == "subscript_expression") {
            // For `a[i] = v`, treat `a` as both a use (we depend on its
            // address-as-name being live) AND a def of the container.
            // Conservatively, we mark the receiver as use+def (so the
            // store does not kill the variable as a whole) and the index
            // as a pure use.
            ::TSNode receiver = ::ts_node_child_by_field_name(lhs, "argument", 8U);
            if (::ts_node_is_null(receiver)) {
                receiver = ::ts_node_child(lhs, 0U);
            }
            walk_subexpr(receiver, block);  // counts as use
            // Walk index: any expression inside the brackets is a use.
            const std::uint32_t cnt = ::ts_node_child_count(lhs);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(lhs, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child) &&
                    !::ts_node_eq(child, receiver)) {
                    walk_subexpr(child, block);
                }
            }
            return;
        }
        if (kind == "field_expression") {
            // `obj.field = v` -- leftmost identifier is a use+def of the
            // receiver. We treat the whole `obj` chain as a use to keep
            // it live; we do not track per-field defs.
            ::TSNode receiver = ::ts_node_child_by_field_name(lhs, "argument", 8U);
            if (::ts_node_is_null(receiver)) {
                receiver = ::ts_node_child(lhs, 0U);
            }
            walk_subexpr(receiver, block);
            return;
        }
        // Default: treat the whole LHS as a use-like expression.
        walk_subexpr(lhs, block);
    }

    /// Walk an arbitrary subexpression collecting uses of every identifier
    /// inside it. The walker pushes children onto a stack rather than
    /// recursing so deeply-nested expressions don't blow the call stack.
    void walk_subexpr(::TSNode root, std::uint32_t block) {
        if (::ts_node_is_null(root)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            const auto kind = node_kind(node);
            if (kind == "identifier") {
                record_use(node, block);
                continue;
            }
            if (kind == "call_expression") {
                // Do not count the callee identifier as a use; classify
                // its arguments as uses.
                const ::TSNode callee = ::ts_node_child_by_field_name(node, "function", 8U);
                if (!::ts_node_is_null(callee)) {
                    use_skip.insert(pack_node(callee));
                    // If the callee is itself a field/identifier expression
                    // such as `obj.method`, the receiver IS a use (we want
                    // it live). Walk the children of the callee but mark
                    // the *outer-most* identifier as skip.
                    const auto callee_kind = node_kind(callee);
                    if (callee_kind == "field_expression") {
                        const ::TSNode receiver =
                            ::ts_node_child_by_field_name(callee, "argument", 8U);
                        if (!::ts_node_is_null(receiver)) {
                            walk_subexpr(receiver, block);
                        }
                    }
                }
                const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9U);
                if (!::ts_node_is_null(args)) {
                    stack.push_back(args);
                }
                continue;
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                    stack.push_back(child);
                }
            }
        }
    }

    /// Walk a single declaration node looking for `init_declarator`s and
    /// their initializers. Each declared name produces a def in the
    /// enclosing block; the initializer expression produces uses.
    void walk_declaration(::TSNode decl, std::uint32_t block) {
        if (::ts_node_is_null(decl)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(decl);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            const auto kind = node_kind(node);
            if (kind == "init_declarator") {
                ::TSNode declarator = ::ts_node_child_by_field_name(node, "declarator", 10U);
                if (::ts_node_is_null(declarator)) {
                    declarator = ::ts_node_child(node, 0U);
                }
                // Unwrap nested array_declarator if present.
                while (!::ts_node_is_null(declarator) &&
                       node_kind(declarator) == "array_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(declarator, "declarator", 10U);
                    if (::ts_node_is_null(inner)) {
                        break;
                    }
                    declarator = inner;
                }
                if (!::ts_node_is_null(declarator) && node_kind(declarator) == "identifier") {
                    record_def(declarator, block);
                }
                ::TSNode value = ::ts_node_child_by_field_name(node, "value", 5U);
                if (::ts_node_is_null(value)) {
                    // Fallback: last named child not equal to declarator.
                    const std::uint32_t cnt = ::ts_node_child_count(node);
                    for (std::uint32_t i = 0; i < cnt; ++i) {
                        const auto child = ::ts_node_child(node, i);
                        if (!::ts_node_is_null(child) && ::ts_node_is_named(child) &&
                            !::ts_node_eq(child, declarator)) {
                            value = child;
                        }
                    }
                }
                if (!::ts_node_is_null(value)) {
                    walk_subexpr(value, block);
                }
                continue;
            }
            // Bare declarator without initializer: still produce a def.
            if (kind == "identifier") {
                // Identifier directly inside a declaration node is the
                // declared name (type lexemes are skipped via
                // `is_reserved_or_type_token`).
                record_def(node, block);
                continue;
            }
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                    stack.push_back(child);
                }
            }
        }
    }

    /// Walk a single statement-level AST node, classifying its components
    /// into the def/use tables for `block`.
    void walk_statement(::TSNode stmt, std::uint32_t block) {
        if (::ts_node_is_null(stmt)) {
            return;
        }
        const auto kind = node_kind(stmt);

        if (kind == "declaration" || kind == "variable_declaration" ||
            kind == "field_declaration") {
            walk_declaration(stmt, block);
            return;
        }
        if (kind == "assignment_expression") {
            const ::TSNode lhs = ::ts_node_child_by_field_name(stmt, "left", 4U);
            const ::TSNode rhs = ::ts_node_child_by_field_name(stmt, "right", 5U);
            // Standard `use[B]` semantics: identifiers used BEFORE any def
            // in the block. For `sum = sum + i;` the RHS use of `sum` and
            // `i` happens before the LHS def of `sum`, so we walk the RHS
            // first. If the LHS itself uses identifiers (subscript / field
            // access), those count as uses too -- those happen
            // syntactically before the actual store, so we walk them
            // here too. Then record the def from the LHS top-level
            // identifier last so it doesn't suppress the prior uses.
            walk_subexpr(rhs, block);
            walk_assignment_lhs(lhs, block);
            return;
        }
        if (kind == "update_expression") {
            // ++x / --x / x++ / x-- -- x is both used and defined.
            std::vector<::TSNode> stack;
            stack.push_back(stmt);
            while (!stack.empty()) {
                const auto node = stack.back();
                stack.pop_back();
                if (node_kind(node) == "identifier") {
                    record_use(node, block);
                    record_def(node, block);
                    continue;
                }
                const std::uint32_t cnt = ::ts_node_child_count(node);
                for (std::uint32_t i = 0; i < cnt; ++i) {
                    const auto child = ::ts_node_child(node, i);
                    if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                        stack.push_back(child);
                    }
                }
            }
            return;
        }
        if (kind == "expression_statement") {
            // Walk the inner expression; pick up assignment/update/call
            // semantics via recursion.
            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                    walk_statement(child, block);
                }
            }
            return;
        }
        if (kind == "return_statement") {
            // Return value uses every identifier in its expression.
            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
                    walk_subexpr(child, block);
                }
            }
            return;
        }
        if (kind == "for_statement" || kind == "while_statement") {
            // The CFG builder allocates header / body / exit blocks at
            // identical spans (the whole for-statement). We dispatch
            // init / condition / update -> header, body -> body block.
            // Without this disambiguation, all four sites collapse into
            // one block and the standard `use[B]` semantics
            // (use-before-def) wrongly kills loop-carried uses.
            const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(stmt));
            const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(stmt));
            const auto header_block = find_nth_block_with_exact_span(*storage, lo, hi, 0U);
            const auto body_block_id = find_nth_block_with_exact_span(*storage, lo, hi, 1U);
            const auto effective_header = header_block != 0U ? header_block : block;
            const auto effective_body = body_block_id != 0U ? body_block_id : effective_header;

            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (::ts_node_is_null(child) || !::ts_node_is_named(child)) {
                    continue;
                }
                const auto child_kind = node_kind(child);
                if (child_kind == "compound_statement" || child_kind == "block") {
                    // Body block: route compound children there. Reset
                    // defs_seen so per-block use/def accounting is fresh.
                    walk_statement(child, effective_body);
                } else if (child_kind == "declaration" || child_kind == "init_declarator") {
                    walk_declaration(child, effective_header);
                } else {
                    // Condition / update / step expressions land in the
                    // header (where the loop test lives in the CFG).
                    walk_subexpr(child, effective_header);
                }
            }
            return;
        }
        if (kind == "do_statement") {
            // Allocation order: body, cond, exit.
            const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(stmt));
            const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(stmt));
            const auto body_block_id = find_nth_block_with_exact_span(*storage, lo, hi, 0U);
            const auto cond_block_id = find_nth_block_with_exact_span(*storage, lo, hi, 1U);
            const auto effective_body = body_block_id != 0U ? body_block_id : block;
            const auto effective_cond = cond_block_id != 0U ? cond_block_id : effective_body;

            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (::ts_node_is_null(child) || !::ts_node_is_named(child)) {
                    continue;
                }
                const auto child_kind = node_kind(child);
                if (child_kind == "compound_statement" || child_kind == "block") {
                    walk_statement(child, effective_body);
                } else {
                    walk_subexpr(child, effective_cond);
                }
            }
            return;
        }
        if (kind == "if_statement" || kind == "switch_statement") {
            // The condition belongs to the block enclosing the if/switch
            // statement (the dispatch block). Bodies dispatch to their
            // respective then/else/case blocks via per-child block lookup.
            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (::ts_node_is_null(child) || !::ts_node_is_named(child)) {
                    continue;
                }
                const auto child_kind = node_kind(child);
                if (child_kind == "compound_statement" || child_kind == "block" ||
                    child_kind == "if_statement" || child_kind == "switch_statement" ||
                    child_kind == "while_statement" || child_kind == "for_statement" ||
                    child_kind == "do_statement" || child_kind == "case_statement" ||
                    child_kind == "default_statement") {
                    walk_statement(child, block_of(child));
                } else {
                    walk_subexpr(child, block);
                }
            }
            return;
        }
        if (kind == "compound_statement" || kind == "block") {
            // Children of a compound stay in the *passed-in* block by
            // default (the block the caller already determined). When a
            // child statement is itself a control-flow boundary
            // (if / for / while / etc.), or a return statement (which
            // the CFG builder allocates a fresh dead-code block for),
            // we look up the child's own enclosing block instead. For
            // ordinary expression / declaration children we honour the
            // caller's block so that for-loop bodies route their
            // statements to the loop's body block (not the loop's
            // header which shares the same span).
            const std::uint32_t cnt = ::ts_node_child_count(stmt);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(stmt, i);
                if (::ts_node_is_null(child) || !::ts_node_is_named(child)) {
                    continue;
                }
                const auto child_kind = node_kind(child);
                if (child_kind == "compound_statement" || child_kind == "block" ||
                    child_kind == "if_statement" || child_kind == "switch_statement" ||
                    child_kind == "for_statement" || child_kind == "while_statement" ||
                    child_kind == "do_statement" || child_kind == "return_statement") {
                    // Nested control-flow / return: pick the smallest
                    // enclosing block. The control-flow handlers above
                    // further disambiguate header / body / exit
                    // themselves.
                    walk_statement(child, block_of(child));
                } else {
                    walk_statement(child, block);
                }
            }
            return;
        }
        // Default fallback: treat the node as a generic expression --
        // every identifier it touches counts as a use.
        walk_subexpr(stmt, block);
    }

    /// Walk every function_definition's body and route statements to the
    /// per-block tables. We deliberately reset the per-function `defs_seen`
    /// memo at the function boundary so that defs from one function do not
    /// suppress uses in another.
    void walk_function_body(::TSNode body, std::uint32_t block) {
        defs_seen.clear();
        use_skip.clear();
        walk_statement(body, block);
    }

    /// Walk the source root, identifying each function_definition and
    /// dispatching its body for classification.
    void walk_source(::TSNode root) {
        if (::ts_node_is_null(root)) {
            return;
        }
        std::vector<::TSNode> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            const auto node = stack.back();
            stack.pop_back();
            if (node_kind(node) == "function_definition") {
                ::TSNode body = ::ts_node_child_by_field_name(node, "body", 4U);
                if (::ts_node_is_null(body)) {
                    const std::uint32_t cnt = ::ts_node_child_count(node);
                    for (std::uint32_t i = 0; i < cnt; ++i) {
                        const auto child = ::ts_node_child(node, i);
                        if (!::ts_node_is_null(child) && node_kind(child) == "compound_statement") {
                            body = child;
                            break;
                        }
                    }
                }
                if (!::ts_node_is_null(body)) {
                    // Function parameters become defs in the entry block.
                    const std::uint32_t fn_cnt = ::ts_node_child_count(node);
                    const std::uint32_t entry_block = block_of(node);
                    for (std::uint32_t i = 0; i < fn_cnt; ++i) {
                        const auto child = ::ts_node_child(node, i);
                        if (::ts_node_is_null(child)) {
                            continue;
                        }
                        const auto kind = node_kind(child);
                        if (kind == "function_declarator") {
                            // Drill into parameter list.
                            const std::uint32_t dc = ::ts_node_child_count(child);
                            for (std::uint32_t j = 0; j < dc; ++j) {
                                const auto p = ::ts_node_child(child, j);
                                if (::ts_node_is_null(p)) {
                                    continue;
                                }
                                const auto pkind = node_kind(p);
                                if (pkind == "parameter_list") {
                                    const std::uint32_t pc = ::ts_node_child_count(p);
                                    for (std::uint32_t k = 0; k < pc; ++k) {
                                        const auto param = ::ts_node_child(p, k);
                                        if (::ts_node_is_null(param)) {
                                            continue;
                                        }
                                        if (node_kind(param) == "parameter_declaration" ||
                                            node_kind(param) == "parameter") {
                                            walk_declaration(param, entry_block);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    walk_function_body(body, block_of(body));
                }
                continue;  // do not descend into nested functions
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

[[nodiscard]] const control_flow::CfgStorage* storage_of(const ControlFlowInfo& cfg) noexcept {
    if (cfg.cfg.impl == nullptr) {
        return nullptr;
    }
    const auto& storage = cfg.cfg.impl->data.storage;
    return storage ? storage.get() : nullptr;
}

/// Convert an ordered vector of (block, set) into the public-output
/// (block, sorted-vec) format. Pairs are sorted by `BasicBlockId.raw()`
/// so callers can `std::lower_bound` them.
[[nodiscard]] std::vector<std::pair<BasicBlockId, std::vector<std::string>>> finalize_table(
    const std::unordered_map<std::uint32_t, std::unordered_set<std::string>>& src) {
    std::vector<std::pair<BasicBlockId, std::vector<std::string>>> out;
    out.reserve(src.size());
    for (const auto& [block, names] : src) {
        std::vector<std::string> sorted{names.begin(), names.end()};
        std::sort(sorted.begin(), sorted.end());
        out.emplace_back(BasicBlockId{block}, std::move(sorted));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.first.raw() < b.first.raw();
    });
    return out;
}

[[nodiscard]] std::span<const std::string> lookup_block(
    const std::vector<std::pair<BasicBlockId, std::vector<std::string>>>& table,
    BasicBlockId block) noexcept {
    const auto it = std::lower_bound(
        table.begin(), table.end(), block, [](const auto& entry, BasicBlockId target) {
            return entry.first.raw() < target.raw();
        });
    if (it == table.end() || it->first.raw() != block.raw()) {
        return {};
    }
    return std::span<const std::string>{it->second.data(), it->second.size()};
}

}  // namespace

std::span<const std::string> LivenessInfo::live_in_at(BasicBlockId block) const noexcept {
    return lookup_block(live_in_per_block, block);
}

std::span<const std::string> LivenessInfo::live_out_at(BasicBlockId block) const noexcept {
    return lookup_block(live_out_per_block, block);
}

LivenessInfo compute_liveness(const ControlFlowInfo& cfg, const AstTree& tree) {
    LivenessInfo result;
    const auto* storage = storage_of(cfg);
    if (storage == nullptr) {
        return result;
    }
    if (tree.raw_tree() == nullptr) {
        return result;
    }
    const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
    if (::ts_node_is_null(root)) {
        return result;
    }

    // Phase 1: extract def/use sets per block via a single AST walk.
    DefUseTables tables;
    Walker walker;
    walker.storage = storage;
    walker.bytes = tree.source_bytes();
    walker.tables = &tables;
    walker.walk_source(root);

    // Phase 2: backward dataflow. We iterate per function so that
    // out-of-function blocks (sentinel function 0) are skipped.
    //
    //   live_in[B]  = use[B] U (live_out[B] - def[B])
    //   live_out[B] = U live_in[succ]  for every successor
    //
    // We seed `live_out[exit] = {}` (no successors / return blocks).
    // Iteration is to fixed-point in reverse order over the blocks
    // (a postorder approximation -- we just sweep until no changes).
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> live_in;
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> live_out;

    // For each function (skip sentinel id 0), iterate to fixed point.
    for (std::size_t fn_id = 1; fn_id < storage->functions.size(); ++fn_id) {
        const auto& fn = storage->functions[fn_id];
        if (fn.error_skipped || fn.blocks.empty()) {
            continue;
        }
        // Map function-local index -> global raw id.
        std::vector<std::uint32_t> local_to_raw(fn.blocks.size(), 0U);
        for (std::uint32_t local = 0; local < fn.blocks.size(); ++local) {
            local_to_raw[local] = storage->global_id(static_cast<std::uint32_t>(fn_id), local);
        }

        // Iterate to fixed point. Bound loop count defensively.
        const std::size_t max_iters = fn.blocks.size() * 8U + 16U;
        bool changed = true;
        for (std::size_t iter = 0; changed && iter < max_iters; ++iter) {
            changed = false;
            // Sweep blocks in reverse-local order -- a cheap approximation
            // of reverse-postorder for backward dataflow.
            for (std::int32_t local = static_cast<std::int32_t>(fn.blocks.size()) - 1; local >= 0;
                 --local) {
                const auto raw = local_to_raw[static_cast<std::size_t>(local)];
                if (raw == 0U) {
                    continue;
                }
                // live_out[B] = U live_in[succ]
                std::unordered_set<std::string> new_out;
                for (const auto succ_local :
                     fn.blocks[static_cast<std::size_t>(local)].successors) {
                    if (succ_local >= fn.blocks.size()) {
                        continue;
                    }
                    const auto succ_raw = local_to_raw[succ_local];
                    if (succ_raw == 0U) {
                        continue;
                    }
                    const auto it = live_in.find(succ_raw);
                    if (it == live_in.end()) {
                        continue;
                    }
                    for (const auto& name : it->second) {
                        new_out.insert(name);
                    }
                }
                // live_in[B] = use[B] U (live_out[B] - def[B])
                std::unordered_set<std::string> new_in;
                const auto use_it = tables.use_per_block.find(raw);
                if (use_it != tables.use_per_block.end()) {
                    for (const auto& name : use_it->second) {
                        new_in.insert(name);
                    }
                }
                const auto def_it = tables.def_per_block.find(raw);
                for (const auto& name : new_out) {
                    if (def_it != tables.def_per_block.end() && def_it->second.contains(name)) {
                        continue;
                    }
                    new_in.insert(name);
                }
                if (new_in != live_in[raw]) {
                    live_in[raw] = std::move(new_in);
                    changed = true;
                }
                if (new_out != live_out[raw]) {
                    live_out[raw] = std::move(new_out);
                    changed = true;
                }
            }
        }
    }

    result.live_in_per_block = finalize_table(live_in);
    result.live_out_per_block = finalize_table(live_out);
    return result;
}

}  // namespace hlsl_clippy::util
