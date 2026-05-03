// groupshared-over-32k-without-attribute
//
// Detects total `groupshared` allocation over 32 KB in a translation unit
// where the entry point lacks SM 6.10's `[GroupSharedLimit(<bytes>)]`
// attribute (proposal 0049 Accepted). On SM 6.10, exceeding the default
// 32 KB cap without the attribute compile-errors; on SM <= 6.9, it
// silently truncates.
//
// Stage: Ast + Reflection. The byte sum is AST-driven (mirrors the
// `groupshared-too-large` template); reflection is only consulted to
// locate entry-point declarations, but the rule fires under either the
// Ast hook because the attribute scan + sum are pure-text. Provides a
// machine-applicable fix that prepends `[GroupSharedLimit(<rounded>)]`
// to the entry-point declaration.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/sm6_10.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-over-32k-without-attribute";
constexpr std::string_view k_category = "sm6_10";
constexpr std::uint32_t k_default_limit = 32U * 1024U;
constexpr std::uint32_t k_round_up_step = 64U * 1024U;  // 64 KB granularity

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] std::uint32_t sizeof_scalar(std::string_view t) noexcept {
    auto component_count = [&](std::size_t prefix_len) -> std::uint32_t {
        if (t.size() <= prefix_len)
            return 1U;
        const char c = t[prefix_len];
        if (c >= '1' && c <= '4')
            return static_cast<std::uint32_t>(c - '0');
        return 0U;
    };
    auto matrix_dims = [&](std::size_t prefix_len) -> std::uint32_t {
        if (t.size() < prefix_len + 3U)
            return 0U;
        const char r = t[prefix_len];
        const char x = t[prefix_len + 1U];
        const char c = t[prefix_len + 2U];
        if ((r >= '1' && r <= '4') && x == 'x' && (c >= '1' && c <= '4'))
            return static_cast<std::uint32_t>((r - '0') * (c - '0'));
        return 0U;
    };
    if (t.starts_with("float")) {
        const auto m = matrix_dims(5U);
        if (m != 0U)
            return 4U * m;
        const auto v = component_count(5U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("uint") && !t.starts_with("uint64")) {
        const auto v = component_count(4U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("int") && !t.starts_with("int64")) {
        const auto v = component_count(3U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("bool")) {
        const auto v = component_count(4U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("half")) {
        const auto v = component_count(4U);
        return v == 0U ? 0U : 2U * v;
    }
    if (t.starts_with("double")) {
        const auto v = component_count(6U);
        return v == 0U ? 0U : 8U * v;
    }
    return 0U;
}

[[nodiscard]] std::uint32_t total_groupshared_bytes(std::string_view bytes) noexcept {
    constexpr std::string_view k_kw = "groupshared";
    std::uint32_t total = 0U;
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(k_kw, pos);
        if (found == std::string_view::npos) {
            break;
        }
        const bool ok_left = (found == 0U) || !is_id_char(bytes[found - 1U]);
        const std::size_t end = found + k_kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        std::size_t i = end;
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t')) {
            ++i;
        }
        if (bytes.size() - i >= 8U && bytes.substr(i, 8U) == "volatile") {
            i += 8U;
            while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t')) {
                ++i;
            }
        }
        const std::size_t type_lo = i;
        while (i < bytes.size() && (is_id_char(bytes[i]) || bytes[i] == 'x')) {
            ++i;
        }
        const auto type_text = bytes.substr(type_lo, i - type_lo);
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t')) {
            ++i;
        }
        while (i < bytes.size() && is_id_char(bytes[i])) {
            ++i;
        }
        std::uint32_t element_count = 1U;
        while (i < bytes.size() && bytes[i] == '[') {
            ++i;
            std::size_t inside_start = i;
            while (i < bytes.size() && bytes[i] != ']') {
                ++i;
            }
            if (i >= bytes.size()) {
                break;
            }
            const auto digits = trim(bytes.substr(inside_start, i - inside_start));
            std::uint32_t v = 0U;
            bool ok = !digits.empty();
            for (const char c : digits) {
                if (c >= '0' && c <= '9') {
                    v = v * 10U + static_cast<std::uint32_t>(c - '0');
                } else {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                element_count = 0U;
                break;
            }
            element_count *= v;
            ++i;
        }
        const std::uint32_t per_elem = sizeof_scalar(type_text);
        if (per_elem != 0U && element_count != 0U) {
            total += per_elem * element_count;
        }
        pos = i;
    }
    return total;
}

/// Find the first compute-/mesh-/amplification-style entry point: a function
/// definition preceded by a `[numthreads(...)]` attribute. Returns null on
/// miss.
[[nodiscard]] ::TSNode find_entry_point_with_numthreads(::TSNode root,
                                                        std::string_view bytes) noexcept {
    if (::ts_node_is_null(root)) {
        return root;
    }
    if (node_kind(root) == "function_definition") {
        const auto fn_lo = static_cast<std::size_t>(::ts_node_start_byte(root));
        // Walk back to the previous statement boundary and look for the
        // `numthreads(` token in the prefix.
        std::size_t i = fn_lo;
        while (i > 0) {
            const char c = bytes[i - 1];
            if (c == ';' || c == '}' || c == '{') {
                break;
            }
            --i;
        }
        const auto prefix = bytes.substr(i, fn_lo - i);
        if (prefix.find("numthreads(") != std::string_view::npos) {
            return root;
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(root);
    for (std::uint32_t k = 0; k < cnt; ++k) {
        const ::TSNode hit = find_entry_point_with_numthreads(::ts_node_child(root, k), bytes);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

class GroupsharedOver32KWithoutAttribute : public Rule {
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
        const auto total = total_groupshared_bytes(bytes);
        if (total <= k_default_limit) {
            return;
        }
        // Look for a `[GroupSharedLimit(...)]` attribute anywhere in source.
        // The helper resolves the attribute on a specific entry point; for
        // this rule's purposes a broader source-level scan is sufficient
        // because the attribute always targets the compute entry.
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        const ::TSNode entry = find_entry_point_with_numthreads(root, bytes);
        if (!::ts_node_is_null(entry)) {
            const auto limit = util::parse_groupshared_limit_attribute(tree, entry);
            if (limit.has_value() && *limit >= total) {
                return;
            }
        } else if (bytes.find("GroupSharedLimit") != std::string_view::npos) {
            // Fall-back: attribute appears somewhere in source but we can't
            // attribute it to an entry point -- be conservative and skip.
            return;
        }

        // Round the suggested limit up to the next 64 KB boundary.
        const std::uint32_t suggested =
            ((total + k_round_up_step - 1U) / k_round_up_step) * k_round_up_step;

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        // Anchor the diagnostic at the entry point if we found one (so the
        // fix can reasonably be inserted in front of it); otherwise at the
        // start of source.
        if (!::ts_node_is_null(entry)) {
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{static_cast<std::uint32_t>(::ts_node_start_byte(entry)),
                                  static_cast<std::uint32_t>(::ts_node_end_byte(entry))},
            };
        } else {
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{0U, 0U},
            };
        }
        diag.message = std::string{"total `groupshared` allocation is "} + std::to_string(total) +
                       " bytes (> 32 KB SM 6.10 default) but the entry point lacks "
                       "`[GroupSharedLimit(N)]` -- compile-errors on SM 6.10 retail and "
                       "silently truncates on SM <= 6.9";

        // Machine-applicable fix: insert `[GroupSharedLimit(<suggested>)]` at
        // the start of the entry-point declaration. Only attached when we
        // located the entry point.
        if (!::ts_node_is_null(entry)) {
            Fix fix;
            fix.machine_applicable = true;
            fix.description = "Insert `[GroupSharedLimit(" + std::to_string(suggested) +
                              ")]` before the entry-point declaration";
            TextEdit edit;
            edit.span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{static_cast<std::uint32_t>(::ts_node_start_byte(entry)),
                                  static_cast<std::uint32_t>(::ts_node_start_byte(entry))},
            };
            edit.replacement = "[GroupSharedLimit(" + std::to_string(suggested) + ")]\n";
            fix.edits.push_back(std::move(edit));
            diag.fixes.push_back(std::move(fix));
        }

        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_over_32k_without_attribute() {
    return std::make_unique<GroupsharedOver32KWithoutAttribute>();
}

}  // namespace hlsl_clippy::rules
