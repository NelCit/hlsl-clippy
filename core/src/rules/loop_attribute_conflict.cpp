// loop-attribute-conflict
//
// Detects two related anti-patterns on `for` / `while` loops:
//
//   (a) Both `[unroll]` and `[loop]` applied to the same loop. The compiler
//       silently picks one (usually `[unroll]`); the other attribute is dead
//       text. Almost always a refactor leftover.
//   (b) `[unroll(N)]` with N greater than a configurable threshold (default
//       32). Aggressive unrolling balloons the program size and pressures the
//       instruction cache; for large N the compiler may silently fall back to
//       a normal loop anyway.
//
// Detection (purely AST, textual):
//   We look for any `for_statement` or `while_statement` node, scan the source
//   text immediately preceding the statement (back to the previous newline or
//   semicolon) for the `[unroll]` / `[loop]` / `[unroll(N)]` attribute spelling,
//   and emit a diagnostic when the conflict or the over-threshold N is observed.
//
// The threshold is hard-coded at 32 for Phase 2; Phase 3+ exposes it via the
// `.hlsl-clippy.toml` config surface.
//
// Rationale for textual attribute scan: tree-sitter-hlsl v0.2.0 has known
// gaps around the `[attr]` bracket syntax (see external/treesitter-version.md);
// reading attributes from the source bytes preceding the statement is the
// most robust path until the grammar gap is fixed.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;

constexpr std::string_view k_rule_id = "loop-attribute-conflict";
constexpr std::string_view k_category = "control-flow";
constexpr std::uint32_t k_unroll_threshold = 32;

/// Walk back from `start` to the closest preceding `;`, `}`, `{`, or
/// start-of-file. Returns the byte offset of the position just after that
/// boundary (i.e., the start of the attribute-bearing prefix that may
/// precede the loop statement).
[[nodiscard]] std::size_t prefix_start(std::string_view bytes, std::size_t start) noexcept {
    std::size_t i = start;
    while (i > 0) {
        const char c = bytes[i - 1];
        if (c == ';' || c == '}' || c == '{')
            break;
        --i;
    }
    return i;
}

/// True if `text` starts with `keyword` followed by either end-of-string or
/// a non-identifier character.
[[nodiscard]] bool match_keyword(std::string_view text, std::string_view keyword) noexcept {
    if (text.size() < keyword.size())
        return false;
    if (text.substr(0, keyword.size()) != keyword)
        return false;
    if (text.size() == keyword.size())
        return true;
    const char c = text[keyword.size()];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Skip ASCII whitespace; return the new index.
[[nodiscard]] std::size_t skip_ws(std::string_view text, std::size_t i) noexcept {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
        ++i;
    }
    return i;
}

/// Information about a parsed `[unroll(N)]` form. `has_unroll` indicates an
/// `[unroll]` attribute (with or without N); `unroll_n` is the parsed N
/// (zero if absent or unparseable).
struct AttrScan {
    bool has_unroll = false;
    bool has_unroll_arg = false;
    bool has_loop = false;
    std::uint32_t unroll_n = 0;
};

/// Scan `prefix` for `[unroll]`, `[unroll(N)]`, and `[loop]` attribute
/// occurrences. The scan is forgiving: it tolerates whitespace inside the
/// brackets and ignores any other attributes (e.g. `[fastopt]`, `[branch]`).
[[nodiscard]] AttrScan scan_attributes(std::string_view prefix) noexcept {
    AttrScan out;
    std::size_t i = 0;
    while (i < prefix.size()) {
        if (prefix[i] != '[') {
            ++i;
            continue;
        }
        std::size_t j = skip_ws(prefix, i + 1);
        if (j >= prefix.size())
            break;
        const auto rest = prefix.substr(j);
        if (match_keyword(rest, "unroll")) {
            out.has_unroll = true;
            j += std::string_view{"unroll"}.size();
            j = skip_ws(prefix, j);
            if (j < prefix.size() && prefix[j] == '(') {
                out.has_unroll_arg = true;
                ++j;
                j = skip_ws(prefix, j);
                std::uint32_t n = 0;
                bool any = false;
                while (j < prefix.size() && prefix[j] >= '0' && prefix[j] <= '9') {
                    const std::uint32_t digit = static_cast<std::uint32_t>(prefix[j] - '0');
                    // Saturate at 1e9 to avoid overflow on absurd inputs.
                    if (n < 1'000'000'000U) {
                        n = n * 10U + digit;
                    }
                    any = true;
                    ++j;
                }
                if (any)
                    out.unroll_n = n;
            }
        } else if (match_keyword(rest, "loop")) {
            out.has_loop = true;
        }
        // Advance past the `[`; the next iteration will resync on the next `[`.
        ++i;
    }
    return out;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    const auto kind = node_kind(node);
    if (kind == "for_statement" || kind == "while_statement" || kind == "do_statement") {
        // Tree-sitter-hlsl may attach `[attr]` as an `hlsl_attribute` child of
        // the loop statement (in which case the loop's start_byte already
        // includes the attribute), or it may emit ERROR nodes and leave the
        // attribute in the textual prefix preceding the loop's start_byte
        // (the start_byte then lands on `for`/`while`/`do`). Cover both: pull
        // the prefix back from the earliest of (start_byte, first
        // hlsl_attribute child start) to the closest preceding `;{}`.
        auto stmt_lo = static_cast<std::size_t>(::ts_node_start_byte(node));
        const std::uint32_t child_count = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < child_count; ++i) {
            const ::TSNode child = ::ts_node_child(node, i);
            if (node_kind(child) == "hlsl_attribute") {
                const auto child_lo = static_cast<std::size_t>(::ts_node_start_byte(child));
                if (child_lo < stmt_lo) {
                    stmt_lo = child_lo;
                }
            } else {
                break;  // attributes are leading; stop after the first non-attribute child.
            }
        }
        const std::size_t pref_lo = prefix_start(bytes, stmt_lo);
        // We scan the prefix even when it is empty (defensive), because the
        // attributes may also live inside the loop node's own byte range -- in
        // that case `stmt_lo` already covers them and the prefix scan still
        // succeeds because we walk `bytes.substr(pref_lo, span_hi - pref_lo)`
        // up to the loop's `for` keyword. We approximate that by scanning up
        // to the first child that is *not* an hlsl_attribute, which lands on
        // the `for`/`while`/`do` keyword's column.
        std::size_t scan_hi = static_cast<std::size_t>(::ts_node_start_byte(node));
        for (std::uint32_t i = 0; i < child_count; ++i) {
            const ::TSNode child = ::ts_node_child(node, i);
            if (node_kind(child) != "hlsl_attribute") {
                scan_hi = static_cast<std::size_t>(::ts_node_start_byte(child));
                break;
            }
            scan_hi = static_cast<std::size_t>(::ts_node_end_byte(child));
        }
        if (pref_lo < scan_hi) {
            const std::string_view prefix = bytes.substr(pref_lo, scan_hi - pref_lo);
            const AttrScan scan = scan_attributes(prefix);

            // (a) [unroll] + [loop] conflict.
            if (scan.has_unroll && scan.has_loop) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(),
                         .bytes = ByteSpan{.lo = static_cast<std::uint32_t>(pref_lo),
                                           .hi = static_cast<std::uint32_t>(scan_hi)}};
                diag.message = std::string{
                    "`[unroll]` and `[loop]` on the same loop conflict -- the "
                    "compiler silently picks one; this is almost always a "
                    "refactor leftover"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "remove either `[unroll]` (to keep the runtime loop) or "
                    "`[loop]` (to keep the unroll); the compiler currently "
                    "ignores one of them silently"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
            // (b) [unroll(N)] with N over threshold.
            else if (scan.has_unroll_arg && scan.unroll_n > k_unroll_threshold) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(),
                         .bytes = ByteSpan{.lo = static_cast<std::uint32_t>(pref_lo),
                                           .hi = static_cast<std::uint32_t>(scan_hi)}};
                diag.message = std::string{"`[unroll(N)]` with N = "} +
                               std::to_string(scan.unroll_n) + " exceeds the portable threshold (" +
                               std::to_string(k_unroll_threshold) +
                               ") -- aggressive unrolling balloons program size and may "
                               "silently fall back to a runtime loop on some compilers";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "consider lowering N, or split the loop -- aggressive unroll "
                    "factors hurt instruction-cache locality and have diminishing "
                    "occupancy returns"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LoopAttributeConflict : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_loop_attribute_conflict() {
    return std::make_unique<LoopAttributeConflict>();
}

}  // namespace hlsl_clippy::rules
