// groupshared-when-registers-suffice
//
// Detects `groupshared T arr[N]` where N is small (default <= 8) and the
// only access pattern is `arr[<thread_id_like>]` -- access is per-thread
// and never reads other lanes' slots, so the array would fit in registers
// if the group were unrolled.
//
// Stage: Reflection. The rule walks the AST handed to `on_reflection`
// because LDS layout introspection through Slang reflection is per-cbuffer
// only; the AST gives us the declarator + access pattern in source order.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-when-registers-suffice";
constexpr std::string_view k_category = "workgroup";
/// Default tunable matches `LintOptions::vgpr_pressure_threshold / 8`
/// (default 64/8 = 8). Anything larger almost certainly belongs in LDS;
/// anything smaller is a candidate for register-only flattening.
constexpr std::uint32_t k_max_elements = 8U;

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

struct GroupsharedDecl {
    std::string name;
    std::uint32_t element_count = 0U;
    std::uint32_t lo = 0U;
    std::uint32_t hi = 0U;
};

void scan_decls(std::string_view bytes, std::vector<GroupsharedDecl>& out) {
    constexpr std::string_view k_kw = "groupshared";
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(k_kw, pos);
        if (found == std::string_view::npos)
            break;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + k_kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        // Skip type token.
        std::size_t i = end;
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        // Skip volatile if present.
        if (bytes.size() - i >= 8U && bytes.substr(i, 8U) == "volatile") {
            i += 8U;
            while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
                ++i;
        }
        while (i < bytes.size() && (is_id_char(bytes[i]) || bytes[i] == 'x'))
            ++i;
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        const std::size_t name_lo = i;
        while (i < bytes.size() && is_id_char(bytes[i]))
            ++i;
        const auto name = bytes.substr(name_lo, i - name_lo);
        std::uint32_t count = 1U;
        bool has_array = false;
        while (i < bytes.size() && bytes[i] == '[') {
            has_array = true;
            ++i;
            std::size_t inside_start = i;
            while (i < bytes.size() && bytes[i] != ']')
                ++i;
            if (i >= bytes.size())
                break;
            const auto digits = trim(bytes.substr(inside_start, i - inside_start));
            std::uint32_t v = 0U;
            bool ok = !digits.empty();
            for (const char c : digits) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                v = v * 10U + static_cast<std::uint32_t>(c - '0');
            }
            if (!ok) {
                count = 0U;
                break;
            }
            count *= v;
            ++i;
        }
        if (has_array && count > 0U && !name.empty()) {
            out.push_back(GroupsharedDecl{
                .name = std::string{name},
                .element_count = count,
                .lo = static_cast<std::uint32_t>(found),
                .hi = static_cast<std::uint32_t>(i),
            });
        }
        pos = i;
    }
}

[[nodiscard]] bool index_is_thread_id_like(std::string_view idx) noexcept {
    // Accept identifiers / expressions whose text matches a per-thread index:
    // `tid`, `gtid`, `dtid`, `groupIndex`, `SV_GroupIndex`, etc.
    static constexpr std::string_view k_markers[] = {
        "tid", "gtid", "dtid", "GroupIndex", "GroupThread", "DispatchThread", "gi", "groupIndex",
    };
    for (const auto m : k_markers) {
        if (idx.find(m) != std::string_view::npos)
            return true;
    }
    return false;
}

class GroupsharedWhenRegistersSuffice : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       [[maybe_unused]] const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        // ADR 0020 sub-phase A v1.3.1 — needs the AST to inspect groupshared
        // subscript receivers. Bail silently when no tree is available
        // (`.slang` until sub-phase B).
        if (tree.raw_tree() == nullptr) {
            return;
        }
        const auto bytes = tree.source_bytes();
        std::vector<GroupsharedDecl> decls;
        scan_decls(bytes, decls);
        if (decls.empty())
            return;

        // For each small array, walk the AST and check that every subscript
        // access matches a per-thread index pattern.
        for (const auto& d : decls) {
            if (d.element_count > k_max_elements)
                continue;
            // Walk every subscript_expression and check receivers.
            std::vector<::TSNode> stack;
            stack.push_back(::ts_tree_root_node(tree.raw_tree()));
            bool only_thread_id = true;
            bool seen_any = false;
            while (!stack.empty()) {
                const auto n = stack.back();
                stack.pop_back();
                if (::ts_node_is_null(n))
                    continue;
                if (node_kind(n) == "subscript_expression") {
                    ::TSNode receiver = ::ts_node_child_by_field_name(n, "argument", 8);
                    if (::ts_node_is_null(receiver)) {
                        receiver = ::ts_node_child(n, 0U);
                    }
                    const auto recv_text = node_text(receiver, bytes);
                    if (recv_text == d.name) {
                        seen_any = true;
                        // Find the index expression (last named child not the
                        // receiver).
                        ::TSNode index{};
                        const auto named = ::ts_node_named_child_count(n);
                        for (std::uint32_t i = 0; i < named; ++i) {
                            const auto child = ::ts_node_named_child(n, i);
                            if (!::ts_node_eq(child, receiver)) {
                                index = child;
                            }
                        }
                        const auto idx_text = node_text(index, bytes);
                        if (!index_is_thread_id_like(idx_text)) {
                            only_thread_id = false;
                            break;
                        }
                    }
                }
                const std::uint32_t cnt = ::ts_node_child_count(n);
                for (std::uint32_t i = 0; i < cnt; ++i) {
                    stack.push_back(::ts_node_child(n, i));
                }
            }
            if (!seen_any || !only_thread_id)
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{d.lo, d.hi},
            };
            diag.message = std::string{"`groupshared "} + d.name + "[" +
                           std::to_string(d.element_count) +
                           "]` has only per-thread access (no cross-lane reads); the array "
                           "would fit in the register file if unrolled, freeing LDS bandwidth "
                           "for genuinely shared state";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_when_registers_suffice() {
    return std::make_unique<GroupsharedWhenRegistersSuffice>();
}

}  // namespace hlsl_clippy::rules
