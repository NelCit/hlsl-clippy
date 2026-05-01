// CFG builder implementation -- walks the tree-sitter AST and emits per-
// function basic blocks + edges. See `cfg_builder.hpp` for the boundary
// rules; this TU is the only place under `core/src/control_flow/` that
// includes `<tree_sitter/api.h>`.
//
// Phase 4a goals (per ADR 0013 §"Sub-phase 4a"):
//   * One block per function entry; new blocks at branch / loop / barrier
//     / discard boundaries.
//   * Tolerate ERROR nodes per-function: any function whose subtree
//     contains an ERROR node gets a degenerate single-block CFG and a
//     `clippy::cfg-skip` warn-severity diagnostic.
//   * Block-spans anchor at real source byte ranges so diagnostics derived
//     from the CFG point at real syntactic locations.
//
// The builder is intentionally conservative -- it does NOT attempt to
// model every HLSL CF nuance. It models:
//   * if / else  -> condition block + then block + else block + join block
//   * for / while -> header block + body block + tail block
//   * do .. while -> body block + condition block + tail block
//   * switch / case -> dispatch block + one block per case label
//   * discard / clip -> ends current block (sets `contains_discard`)
//   * GroupMemoryBarrier* / DeviceMemoryBarrier* -> ends current block
//     (sets `contains_barrier`)
//   * return -> ends current block, tagged `is_exit`, no successor

#include "control_flow/cfg_builder.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/diagnostic.hpp"
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

[[nodiscard]] Span node_span(::TSNode node, SourceId source) noexcept {
    if (::ts_node_is_null(node)) {
        return Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    }
    return Span{
        .source = source,
        .bytes =
            ByteSpan{
                .lo = static_cast<std::uint32_t>(::ts_node_start_byte(node)),
                .hi = static_cast<std::uint32_t>(::ts_node_end_byte(node)),
            },
    };
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

/// Recognise a barrier-call expression: `GroupMemoryBarrier*` or
/// `DeviceMemoryBarrier*` (or the legacy `AllMemoryBarrier*` family). We
/// match by call-target identifier text rather than by AST shape so we
/// catch both the bare-call and the `Sync` suffixed variants.
[[nodiscard]] bool is_barrier_call(std::string_view ident) noexcept {
    return ident == "GroupMemoryBarrier" || ident == "GroupMemoryBarrierWithGroupSync" ||
           ident == "DeviceMemoryBarrier" || ident == "DeviceMemoryBarrierWithGroupSync" ||
           ident == "AllMemoryBarrier" || ident == "AllMemoryBarrierWithGroupSync";
}

/// True when the node carries any ERROR descendant. Mirrors
/// `AstCursor::has_error()` so the builder can decide per-function whether
/// to skip CFG construction.
[[nodiscard]] bool subtree_has_error(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return false;
    }
    return ::ts_node_has_error(node);
}

/// Collect every named child statement of `compound`. Used for body-block
/// linearisation -- we walk the children in source order and split the
/// current block whenever we hit a statement that introduces a new
/// boundary.
void collect_named_children(::TSNode parent, std::vector<::TSNode>& out) {
    const std::uint32_t count = ::ts_node_child_count(parent);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(parent, i);
        if (!::ts_node_is_null(child) && ::ts_node_is_named(child)) {
            out.push_back(child);
        }
    }
}

/// Light per-function builder state. Holds the CfgFunction being built and
/// a "current block" cursor so the recursive walker can append successors
/// in source order.
struct FunctionBuilder {
    CfgStorage* storage = nullptr;
    std::uint32_t function_id = 0U;
    SourceId source;
    std::string_view bytes;

    [[nodiscard]] CfgFunction& fn() noexcept {
        return storage->functions[function_id];
    }

    /// Allocate a new function-local block, register its global id, and
    /// return its local index.
    [[nodiscard]] std::uint32_t new_block(::TSNode anchor) {
        BasicBlock block;
        block.span = node_span(anchor, source);
        const auto local = static_cast<std::uint32_t>(fn().blocks.size());
        fn().blocks.push_back(block);
        const auto raw = storage->allocate_block_id(function_id, local);
        storage->span_to_block.emplace_back(block.span, raw);
        return local;
    }

    void add_edge(std::uint32_t from, std::uint32_t to) {
        if (from >= fn().blocks.size()) {
            return;
        }
        fn().blocks[from].successors.push_back(to);
    }

    /// Detect a barrier call inside `node` (recursive scan one level deep
    /// into call_expression). We stop at function boundaries so nested
    /// lambdas don't flag the parent.
    [[nodiscard]] bool detect_barrier(::TSNode node) const noexcept {
        if (::ts_node_is_null(node)) {
            return false;
        }
        if (node_kind(node) == "call_expression") {
            const ::TSNode callee = ::ts_node_child_by_field_name(node, "function", 8U);
            if (!::ts_node_is_null(callee)) {
                if (is_barrier_call(node_text(callee, bytes))) {
                    return true;
                }
            }
        }
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const ::TSNode child = ::ts_node_child(node, i);
            if (::ts_node_is_null(child)) {
                continue;
            }
            const auto kind = node_kind(child);
            if (kind == "function_definition") {
                continue;  // don't descend into nested functions
            }
            if (detect_barrier(child)) {
                return true;
            }
        }
        return false;
    }

    /// Walk one statement. Mutates `current` in place to reflect the block
    /// the next statement should append to. Returns the (possibly new)
    /// current block index.
    [[nodiscard]] std::uint32_t walk_statement(::TSNode stmt, std::uint32_t current) {
        if (::ts_node_is_null(stmt)) {
            return current;
        }
        const auto kind = node_kind(stmt);

        // Barrier / discard / return mark the current block and start a new
        // one (except return, which terminates this control path).
        if (kind == "return_statement") {
            fn().blocks[current].is_exit = true;
            // Allocate a fresh block to absorb any following dead code.
            const auto next = new_block(stmt);
            return next;
        }

        if (detect_barrier(stmt)) {
            fn().blocks[current].contains_barrier = true;
            const auto next = new_block(stmt);
            add_edge(current, next);
            return next;
        }

        // Discard (and the older `clip` form) is matched by an identifier
        // text scan -- tree-sitter-hlsl's grammar represents `discard;` as
        // a statement that contains an identifier of value "discard". We
        // also catch the call form `clip(value)` defensively.
        const auto stmt_text = node_text(stmt, bytes);
        if (stmt_text.find("discard") != std::string_view::npos || stmt_text.starts_with("clip(") ||
            stmt_text.find(" clip(") != std::string_view::npos) {
            // Conservative: a textual hit is enough to mark the block.
            // Phase 4 helper-lane analysis tightens this with structural
            // matching.
            fn().blocks[current].contains_discard = true;
        }

        if (kind == "if_statement") {
            const auto cond_block = current;
            // Consequence and alternative are tree-sitter-hlsl field names;
            // some grammar variants use unnamed children, in which case we
            // walk children sequentially as a fallback.
            ::TSNode then_node = ::ts_node_child_by_field_name(stmt, "consequence", 11U);
            ::TSNode else_node = ::ts_node_child_by_field_name(stmt, "alternative", 11U);
            if (::ts_node_is_null(then_node) && ::ts_node_is_null(else_node)) {
                std::vector<::TSNode> kids;
                collect_named_children(stmt, kids);
                // Layout: condition, then_branch, optionally else_branch.
                if (kids.size() >= 2U) {
                    then_node = kids[1];
                }
                if (kids.size() >= 3U) {
                    else_node = kids[2];
                }
            }

            const auto then_block = new_block(stmt);
            add_edge(cond_block, then_block);
            auto then_end = walk_statement(then_node, then_block);

            std::uint32_t else_end = cond_block;
            if (!::ts_node_is_null(else_node)) {
                const auto else_block = new_block(else_node);
                add_edge(cond_block, else_block);
                else_end = walk_statement(else_node, else_block);
            }

            const auto join = new_block(stmt);
            add_edge(then_end, join);
            if (!::ts_node_is_null(else_node)) {
                add_edge(else_end, join);
            } else {
                // No else arm: condition can fall through directly.
                add_edge(cond_block, join);
            }
            return join;
        }

        if (kind == "for_statement" || kind == "while_statement") {
            const auto pre_loop = current;
            const auto header = new_block(stmt);
            add_edge(pre_loop, header);
            const auto body = new_block(stmt);
            add_edge(header, body);
            // Body fallthrough returns to the header (loop back-edge).
            ::TSNode body_node = ::ts_node_child_by_field_name(stmt, "body", 4U);
            if (::ts_node_is_null(body_node)) {
                std::vector<::TSNode> kids;
                collect_named_children(stmt, kids);
                if (!kids.empty()) {
                    body_node = kids.back();
                }
            }
            const auto body_end = walk_statement(body_node, body);
            add_edge(body_end, header);  // back edge
            const auto exit_block = new_block(stmt);
            add_edge(header, exit_block);  // loop exit
            return exit_block;
        }

        if (kind == "do_statement") {
            const auto pre_loop = current;
            const auto body = new_block(stmt);
            add_edge(pre_loop, body);
            ::TSNode body_node = ::ts_node_child_by_field_name(stmt, "body", 4U);
            if (::ts_node_is_null(body_node)) {
                std::vector<::TSNode> kids;
                collect_named_children(stmt, kids);
                if (!kids.empty()) {
                    body_node = kids.front();
                }
            }
            const auto body_end = walk_statement(body_node, body);
            const auto cond_block = new_block(stmt);
            add_edge(body_end, cond_block);
            add_edge(cond_block, body);  // back edge
            const auto exit_block = new_block(stmt);
            add_edge(cond_block, exit_block);
            return exit_block;
        }

        if (kind == "switch_statement") {
            const auto dispatch = current;
            std::vector<::TSNode> kids;
            collect_named_children(stmt, kids);
            const auto join = new_block(stmt);
            for (const auto child : kids) {
                if (node_kind(child) == "case_statement" ||
                    node_kind(child) == "default_statement") {
                    const auto case_block = new_block(child);
                    add_edge(dispatch, case_block);
                    const auto case_end = walk_statement(child, case_block);
                    add_edge(case_end, join);
                }
            }
            // Defensive: dispatch can fall through to join if no cases
            // matched (or grammar produced no case children).
            if (kids.empty()) {
                add_edge(dispatch, join);
            }
            return join;
        }

        if (kind == "compound_statement") {
            // Linearise the children inside the brace block. Each
            // boundary-introducing statement may split `current` further.
            std::vector<::TSNode> kids;
            collect_named_children(stmt, kids);
            auto cur = current;
            for (const auto child : kids) {
                cur = walk_statement(child, cur);
            }
            return cur;
        }

        // Default: statement does not introduce a boundary; stay in the
        // current block.
        return current;
    }
};

/// Build one CfgFunction for `fn_node` (a `function_definition` AST node).
/// `function_id` indexes into `storage.functions` and is already allocated.
void build_function(CfgStorage& storage,
                    std::uint32_t function_id,
                    ::TSNode fn_node,
                    SourceId source,
                    std::string_view bytes,
                    std::vector<Diagnostic>& diagnostics) {
    auto& cfn = storage.functions[function_id];
    cfn.declaration_span = node_span(fn_node, source);

    // Best-effort name extraction: walk children for the identifier.
    const std::uint32_t count = ::ts_node_child_count(fn_node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(fn_node, i);
        if (::ts_node_is_null(child)) {
            continue;
        }
        const auto kind = node_kind(child);
        if (kind == "function_declarator") {
            const ::TSNode ident = ::ts_node_child_by_field_name(child, "declarator", 10U);
            if (!::ts_node_is_null(ident)) {
                cfn.name = std::string{node_text(ident, bytes)};
                break;
            }
        }
        if (kind == "identifier" && cfn.name.empty()) {
            cfn.name = std::string{node_text(child, bytes)};
        }
    }

    // ERROR-tolerance: if the function subtree contains any ERROR node,
    // emit a degenerate single-block CFG and warn.
    if (subtree_has_error(fn_node)) {
        cfn.error_skipped = true;
        BasicBlock entry;
        entry.span = cfn.declaration_span;
        entry.tainted_by_error = true;
        cfn.blocks.push_back(entry);
        const auto raw = storage.allocate_block_id(function_id, 0U);
        storage.span_to_block.emplace_back(entry.span, raw);
        cfn.idom.assign(1U, 0U);

        Diagnostic diag;
        diag.code = std::string{"clippy::cfg-skip"};
        diag.severity = Severity::Warning;
        diag.primary_span = cfn.declaration_span;
        diag.message =
            std::string{"function contains a tree-sitter ERROR node; skipping CFG-stage rules "} +
            std::string{"for this function (see ADR 0013 risk mitigation; ADR 0002 grammar gaps)"};
        diagnostics.push_back(std::move(diag));
        return;
    }

    // Normal path: allocate the entry block (anchored at the function
    // declaration span) and walk the body.
    FunctionBuilder fb;
    fb.storage = &storage;
    fb.function_id = function_id;
    fb.source = source;
    fb.bytes = bytes;
    const auto entry_local = fb.new_block(fn_node);

    // Locate the body (compound_statement) child.
    ::TSNode body = ::ts_node_child_by_field_name(fn_node, "body", 4U);
    if (::ts_node_is_null(body)) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const ::TSNode child = ::ts_node_child(fn_node, i);
            if (!::ts_node_is_null(child) && node_kind(child) == "compound_statement") {
                body = child;
                break;
            }
        }
    }

    if (!::ts_node_is_null(body)) {
        const auto end_local = fb.walk_statement(body, entry_local);
        cfn.blocks[end_local].is_exit = true;
    } else {
        cfn.blocks[entry_local].is_exit = true;
    }
}

/// Recursively discover every `function_definition` in the source. We don't
/// descend into a function once we've found it (HLSL doesn't support
/// nested functions in practice; defending against it just keeps the block
/// counts honest if the grammar reports one).
void collect_functions(::TSNode node, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        out.push_back(node);
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_functions(::ts_node_child(node, i), out);
    }
}

}  // namespace

BuildResult build_cfg(::TSNode root, SourceId source, std::string_view bytes) {
    BuildResult result;
    result.storage = std::make_shared<CfgStorage>();
    result.storage->source = source;

    // Sentinel function 0 -- present so global block id 0 stays "invalid".
    result.storage->functions.emplace_back();

    if (::ts_node_is_null(root)) {
        return result;
    }

    std::vector<::TSNode> fn_nodes;
    collect_functions(root, fn_nodes);

    for (const auto fn_node : fn_nodes) {
        const auto function_id = static_cast<std::uint32_t>(result.storage->functions.size());
        result.storage->functions.emplace_back();
        build_function(*result.storage, function_id, fn_node, source, bytes, result.diagnostics);
    }

    return result;
}

}  // namespace hlsl_clippy::control_flow
