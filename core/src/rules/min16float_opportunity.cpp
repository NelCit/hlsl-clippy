// min16float-opportunity
//
// Detects ALU expressions of the form `(float)half_input * literal_constant`
// where the constant fits in 16 bits. Such chains can be downgraded to
// `min16float` arithmetic, doubling vector throughput on every IHV that
// supports packed-fp16 (RDNA 1+, Turing+, Xe-HPG). The rule mirrors
// `min16float-in-cbuffer-roundtrip` in spirit but for the OPPOSITE direction
// -- here, adding a `min16float` cast WOULD save work because the surrounding
// chain stays small enough for fp16.
//
// Stage: Ast + Reflection. The lookup uses reflection to identify
// half-typed cbuffer fields so we don't false-positive on plain-`float`
// pipelines; if reflection cannot resolve a binding, the rule scans for
// `min16float` / `half` declarations in source.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "min16float-opportunity";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool is_small_literal(std::string_view s) noexcept {
    // Strip suffixes / decimal point / sign.
    if (s.empty())
        return false;
    if (s.front() == '-' || s.front() == '+')
        s.remove_prefix(1U);
    if (s.empty())
        return false;
    // Accept integer or simple decimal in [0, 65504] (fp16 max).
    bool has_digit = false;
    bool past_dot = false;
    std::uint32_t int_part = 0U;
    for (const char c : s) {
        if (c == '.') {
            if (past_dot)
                return false;
            past_dot = true;
            continue;
        }
        if (c == 'f' || c == 'F' || c == 'h' || c == 'H')
            break;
        if (c < '0' || c > '9')
            return false;
        if (!past_dot) {
            int_part = int_part * 10U + static_cast<std::uint32_t>(c - '0');
            if (int_part > 65504U)
                return false;
        }
        has_digit = true;
    }
    return has_digit;
}

void walk(::TSNode node,
          std::string_view bytes,
          const std::vector<std::string>& half_idents,
          const AstTree& tree,
          RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "binary_expression") {
        const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
        const auto op_text = node_text(op, bytes);
        if (op_text == "*") {
            const auto lhs = ::ts_node_child_by_field_name(node, "left", 4);
            const auto rhs = ::ts_node_child_by_field_name(node, "right", 5);
            const auto lhs_text = node_text(lhs, bytes);
            const auto rhs_text = node_text(rhs, bytes);
            // Look for `(float)X * literal` or `literal * (float)X`.
            auto matches_cast = [&](std::string_view t) {
                if (t.find("(float)") == std::string_view::npos)
                    return false;
                for (const auto& name : half_idents) {
                    if (t.find(name) != std::string_view::npos)
                        return true;
                }
                return false;
            };
            const bool left_cast = matches_cast(lhs_text);
            const bool right_cast = matches_cast(rhs_text);
            const bool left_lit = is_small_literal(lhs_text);
            const bool right_lit = is_small_literal(rhs_text);
            if ((left_cast && right_lit) || (right_cast && left_lit)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "ALU chain widens a `min16float` / `half` value to 32-bit and multiplies "
                    "by a 16-bit-representable constant -- consider keeping the chain in "
                    "`min16float` to double packed-fp16 throughput on RDNA 1+, Turing+, and "
                    "Xe-HPG"};
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, half_idents, tree, ctx);
    }
}

class Min16FloatOpportunity : public Rule {
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
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        // ADR 0020 sub-phase A v1.3.1 — needs the AST to find use sites of
        // half-typed identifiers. Bail silently when no tree is available
        // (`.slang` until sub-phase B).
        if (tree.raw_tree() == nullptr) {
            return;
        }
        const auto bytes = tree.source_bytes();
        // Collect every cbuffer field whose type is half / min16; identifier
        // text in the source matches that field name will trigger the rule.
        std::vector<std::string> half_idents;
        for (const auto& cb : reflection.cbuffers) {
            for (const auto& field : cb.fields) {
                if (field.type_name.starts_with("half") || field.type_name.starts_with("min16")) {
                    half_idents.push_back(field.name);
                }
            }
        }
        // Also accept locally-declared `min16float` / `half` identifiers.
        // Cheap textual scan for declarators.
        std::size_t pos = 0U;
        constexpr std::array<std::string_view, 2> k_keys = {"min16float", "half"};
        for (const auto kw : k_keys) {
            pos = 0U;
            while (pos < bytes.size()) {
                const auto found = bytes.find(kw, pos);
                if (found == std::string_view::npos)
                    break;
                const std::size_t end = found + kw.size();
                if (end < bytes.size() && (bytes[end] == ' ' || bytes[end] == '\t')) {
                    // Skip type suffix component digits.
                    std::size_t i = end;
                    while (i < bytes.size() &&
                           (bytes[i] == ' ' || bytes[i] == '\t' ||
                            (bytes[i] >= '0' && bytes[i] <= '9') || bytes[i] == 'x'))
                        ++i;
                    const std::size_t name_start = i;
                    while (i < bytes.size() && util::is_id_char(bytes[i]))
                        ++i;
                    if (i > name_start) {
                        half_idents.emplace_back(bytes.substr(name_start, i - name_start));
                    }
                }
                pos = end;
            }
        }
        if (half_idents.empty())
            return;
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, half_idents, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_min16float_opportunity() {
    return std::make_unique<Min16FloatOpportunity>();
}

}  // namespace shader_clippy::rules
