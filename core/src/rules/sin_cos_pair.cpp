// sin-cos-pair
//
// Detects separate sin(x) and cos(x) calls on the same argument within the
// same compound statement (function body). On every modern GPU the hardware
// sincos unit computes both simultaneously for the same instruction cost, so
// the rewrite `sincos(x, s, c)` halves the transcendental cost.
//
// The match is imperative: we walk every compound_statement in the tree, build
// a map from (argument text) -> (node that produced sin/cos), and fire when
// both a sin and a cos of the same argument are found.
//
// Restriction: the argument must be a pure expression to avoid false positives
// on side-effecting arguments like `sin(rng())` / `cos(rng())`. Pure means:
// no call_expressions in the argument tree (only identifiers, literals, field
// accesses, and binary/unary arithmetic on those). The fix is SUGGESTION-ONLY
// so we can be moderately liberal here — the user confirms before applying.
//
// The fix is SUGGESTION-ONLY because introducing sincos() requires emitting
// two output variables whose names must not collide with existing declarations.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "sin-cos-pair";
constexpr std::string_view k_category = "math";

/// True if `node` is a pure (side-effect-free) expression. We accept any
/// expression that does not contain a call_expression, since calls may have
/// side effects. This admits identifiers, literals, field accesses, and
/// arithmetic expressions of those (e.g. `phase + time`, `uv.x * 2.0`).
[[nodiscard]] bool is_pure_arg(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const char* t = ::ts_node_type(node);
    if (t == nullptr)
        return false;
    const std::string_view kind{t};
    // Any call expression is impure.
    if (kind == "call_expression")
        return false;
    // Recursively check all children.
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        if (!is_pure_arg(::ts_node_child(node, i)))
            return false;
    }
    return true;
}

/// Return the text of a TSNode as a string_view into `bytes`.
[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

/// True if `node` is a call_expression to `fn_name` with exactly one argument.
/// Returns the (sole) argument node via out-param if so.
[[nodiscard]] bool is_unary_call_to(::TSNode node,
                                    std::string_view bytes,
                                    std::string_view fn_name,
                                    ::TSNode& arg_out) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const char* t = ::ts_node_type(node);
    if (t == nullptr || std::string_view{t} != "call_expression")
        return false;

    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (::ts_node_is_null(fn) || node_text(fn, bytes) != fn_name)
        return false;

    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 1U)
        return false;

    arg_out = ::ts_node_named_child(args, 0);
    return !::ts_node_is_null(arg_out);
}

struct SinCosEntry {
    ::TSNode sin_call{};  ///< The sin(...) call node (null if not seen).
    ::TSNode cos_call{};  ///< The cos(...) call node (null if not seen).
};

/// Recursively walk all direct statement-children of `compound` and collect
/// sin/cos calls. We only look one level deep per compound_statement so that
/// nested functions don't pollute the outer scope's map.
void collect_sin_cos(::TSNode compound,
                     std::string_view bytes,
                     std::map<std::string, SinCosEntry>& entries) {
    const uint32_t count = ::ts_node_child_count(compound);
    for (uint32_t i = 0; i < count; ++i) {
        const ::TSNode stmt = ::ts_node_child(compound, i);
        if (::ts_node_is_null(stmt) || !::ts_node_is_named(stmt))
            continue;
        // Walk descendants within this statement to find call_expression nodes.
        // We use a simple iterative descent.
        std::vector<::TSNode> stack;
        stack.push_back(stmt);
        while (!stack.empty()) {
            const ::TSNode cur = stack.back();
            stack.pop_back();
            if (::ts_node_is_null(cur))
                continue;

            // If we hit a nested compound_statement we stop (different scope).
            const char* ct = ::ts_node_type(cur);
            if (ct != nullptr && std::string_view{ct} == "compound_statement" &&
                cur.id != compound.id) {
                continue;  // don't descend into nested scopes
            }

            ::TSNode arg{};
            if (is_unary_call_to(cur, bytes, "sin", arg)) {
                if (is_pure_arg(arg)) {
                    const auto key = std::string{node_text(arg, bytes)};
                    if (!::ts_node_is_null(entries[key].sin_call)) {
                        // Already have a sin — prefer keeping the first one.
                    } else {
                        entries[key].sin_call = cur;
                    }
                }
            } else if (is_unary_call_to(cur, bytes, "cos", arg)) {
                if (is_pure_arg(arg)) {
                    const auto key = std::string{node_text(arg, bytes)};
                    if (!::ts_node_is_null(entries[key].cos_call)) {
                        // Already have a cos — keep first.
                    } else {
                        entries[key].cos_call = cur;
                    }
                }
            }

            // Push children for further descent.
            const uint32_t nc = ::ts_node_child_count(cur);
            for (uint32_t j = 0; j < nc; ++j) {
                const ::TSNode child = ::ts_node_child(cur, j);
                if (!::ts_node_is_null(child))
                    stack.push_back(child);
            }
        }
    }
}

/// Recursively walk the tree, visiting every compound_statement.
void walk_compound_statements(::TSNode node,
                              std::string_view bytes,
                              const AstTree& tree,
                              RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    const char* t = ::ts_node_type(node);
    if (t != nullptr && std::string_view{t} == "compound_statement") {
        // Collect sin/cos calls within this scope.
        std::map<std::string, SinCosEntry> entries;
        collect_sin_cos(node, bytes, entries);

        for (const auto& [arg_text, entry] : entries) {
            if (::ts_node_is_null(entry.sin_call) || ::ts_node_is_null(entry.cos_call)) {
                continue;
            }
            // Both sin and cos found for the same argument — fire on the sin call.
            const auto sin_range = tree.byte_range(entry.sin_call);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = sin_range};
            diag.message =
                std::string{"`sin("} + arg_text + ")` and `cos(" + arg_text +
                ")` are both present — use `sincos(" + arg_text +
                ", s, c)` to compute both for the cost of one transcendental instruction";

            Fix fix;
            fix.machine_applicable = false;
            fix.description =
                std::string{"introduce `float s, c; sincos("} + arg_text +
                ", s, c);` and replace the individual sin/cos calls with `s` and `c`; "
                "choose variable names that do not conflict with existing declarations";
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }

    // Recurse into all children.
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk_compound_statements(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SinCosPair : public Rule {
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
        const auto bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        walk_compound_statements(root, bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_sin_cos_pair() {
    return std::make_unique<SinCosPair>();
}

}  // namespace hlsl_clippy::rules
