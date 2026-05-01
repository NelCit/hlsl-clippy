// dead-store-sv-target
//
// Detects pixel shaders that write a value to an `SV_Target*` output and
// then unconditionally overwrite it before returning. The first write is
// dead -- the compiler discards the assignment, but the source-level
// duplication confuses readers and obscures intent.
//
// Detection plan (AST-only): walk top-level function bodies. For each named
// output assigned via `output.<field> = ...` where `<field>` is annotated
// `SV_Target*` in the struct definition, count consecutive writes to the
// same field with no intervening read. If two writes appear in straight-
// line code with no branch / call / discard between them, emit on the first
// write. We approximate "straight-line code" by tracking matching `{` / `}`
// nesting and disregarding writes inside an `if` / `for` / `while` body.
//
// Implementation kept narrow: this rule fires on the canonical
// `output.color = ...; output.color = ...;` pattern at the same brace
// depth, in document order, with no brace-depth change between them.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "dead-store-sv-target";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// Read a token (identifier) ending at byte `i`. Returns the byte range
/// covering the token, or {i, i} if the previous character is not an
/// identifier char.
[[nodiscard]] std::pair<std::size_t, std::size_t> token_ending_at(std::string_view bytes,
                                                                  std::size_t i) noexcept {
    if (i == 0U || !is_id_char(bytes[i - 1U]))
        return {i, i};
    std::size_t start = i;
    while (start > 0U && is_id_char(bytes[start - 1U]))
        --start;
    return {start, i};
}

/// Collect names of struct fields annotated with `SV_Target*` in the source.
[[nodiscard]] std::vector<std::string> collect_sv_target_fields(std::string_view bytes) {
    std::vector<std::string> out;
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find("SV_Target", pos);
        if (found == std::string_view::npos)
            return out;
        // Walk back from `:` colon to find the field name.
        // Pattern: `<type> <name> : SV_TargetN`.
        std::size_t colon = found;
        while (colon > 0U && bytes[colon - 1U] != ':')
            --colon;
        if (colon == 0U) {
            pos = found + 1U;
            continue;
        }
        std::size_t k = colon - 1U;  // at the ':'
        if (k > 0U)
            --k;
        while (k > 0U && (bytes[k] == ' ' || bytes[k] == '\t'))
            --k;
        // bytes[k] should be the last char of the field name.
        const auto [name_lo, name_hi] = token_ending_at(bytes, k + 1U);
        if (name_lo < name_hi) {
            out.emplace_back(bytes.substr(name_lo, name_hi - name_lo));
        }
        pos = found + 1U;
    }
    return out;
}

class DeadStoreSvTarget : public Rule {
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
        const auto fields = collect_sv_target_fields(bytes);
        if (fields.empty())
            return;
        // For each candidate field, find consecutive `... .<field> = ...;`
        // assignments at the same brace depth.
        for (const auto& field : fields) {
            // Walk the source tracking brace depth.
            int depth = 0;
            // Map from brace-depth to (last_assignment_byte, last_after_token_byte).
            std::vector<std::pair<std::uint32_t, std::uint32_t>> last_assign;
            last_assign.resize(64U, {std::uint32_t{0}, std::uint32_t{0}});
            std::vector<bool> last_valid(64U, false);
            std::size_t i = 0U;
            while (i < bytes.size()) {
                const char c = bytes[i];
                if (c == '{') {
                    ++depth;
                    if (depth >= static_cast<int>(last_assign.size())) {
                        last_assign.resize(static_cast<std::size_t>(depth) + 16U,
                                           {std::uint32_t{0}, std::uint32_t{0}});
                        last_valid.resize(last_assign.size(), false);
                    }
                    last_valid[static_cast<std::size_t>(depth)] = false;
                    ++i;
                    continue;
                }
                if (c == '}') {
                    if (depth >= 0 && static_cast<std::size_t>(depth) < last_valid.size())
                        last_valid[static_cast<std::size_t>(depth)] = false;
                    if (depth > 0)
                        --depth;
                    ++i;
                    continue;
                }
                // Look for `.<field>` followed by whitespace and `=` (not `==`).
                if (c != '.') {
                    if (c == ';') {
                        // Statement boundary: clear the last_assign at this
                        // depth iff the prior assignment was completed on a
                        // semicolon. We keep last_valid set here so that two
                        // adjacent assignments separated by `;` still chain.
                    }
                    if (c == 'i' && i + 2U < bytes.size() && bytes[i + 1U] == 'f' &&
                        (i + 2U >= bytes.size() || !is_id_char(bytes[i + 2U]))) {
                        // Entering an `if` -- invalidate last_assign at this depth.
                        if (depth >= 0 && static_cast<std::size_t>(depth) < last_valid.size())
                            last_valid[static_cast<std::size_t>(depth)] = false;
                    }
                    ++i;
                    continue;
                }
                const std::size_t name_start = i + 1U;
                std::size_t j = name_start;
                while (j < bytes.size() && is_id_char(bytes[j]))
                    ++j;
                if (bytes.substr(name_start, j - name_start) != field) {
                    i = j;
                    continue;
                }
                std::size_t k = j;
                while (k < bytes.size() && (bytes[k] == ' ' || bytes[k] == '\t'))
                    ++k;
                if (k >= bytes.size() || bytes[k] != '=' ||
                    (k + 1U < bytes.size() && bytes[k + 1U] == '=')) {
                    i = j;
                    continue;
                }
                // Find the statement terminator.
                std::size_t stmt_end = bytes.find(';', k);
                if (stmt_end == std::string_view::npos)
                    break;
                if (depth >= 0 && static_cast<std::size_t>(depth) < last_valid.size() &&
                    last_valid[static_cast<std::size_t>(depth)]) {
                    // Two assignments to the same field at the same depth
                    // with no intervening branch/scope -> emit on the FIRST.
                    const auto& prev = last_assign[static_cast<std::size_t>(depth)];
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{.source = tree.source_id(),
                                             .bytes = ByteSpan{prev.first, prev.second}};
                    diag.message = std::string{"write to `."} + field +
                                   "` (an `SV_Target*` field) is overwritten by a later write at "
                                   "the same scope before the function returns -- the first write "
                                   "is dead";
                    ctx.emit(std::move(diag));
                }
                if (depth >= 0 && static_cast<std::size_t>(depth) < last_valid.size()) {
                    last_assign[static_cast<std::size_t>(depth)] = {
                        static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(stmt_end + 1U)};
                    last_valid[static_cast<std::size_t>(depth)] = true;
                }
                i = stmt_end + 1U;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_dead_store_sv_target() {
    return std::make_unique<DeadStoreSvTarget>();
}

}  // namespace hlsl_clippy::rules
