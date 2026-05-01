// div-without-epsilon
//
// Detects `x / length(...)` and `x / dot(...)` expressions where the divisor
// can hit zero (length of a possibly-zero vector / dot of orthogonal vectors)
// without an epsilon guard such as `max(epsilon, ...)`.
//
// Stage: Ast. The detection is textual on `binary_expression` nodes whose
// operator is `/`.

#include <cstdint>
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

constexpr std::string_view k_rule_id = "div-without-epsilon";
constexpr std::string_view k_category = "math";

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] bool divisor_is_unguarded_length_or_dot(std::string_view rhs) noexcept {
    // Trim whitespace and leading parentheses.
    while (!rhs.empty() && (rhs.front() == ' ' || rhs.front() == '\t' || rhs.front() == '(')) {
        rhs.remove_prefix(1);
    }
    // Reject obvious guards.
    if (rhs.starts_with("max(") || rhs.starts_with("max ("))
        return false;
    // Match `length(` / `dot(` at the start.
    return rhs.starts_with("length(") || rhs.starts_with("length (") || rhs.starts_with("dot(") ||
           rhs.starts_with("dot (");
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression") {
        const auto op = ::ts_node_child_by_field_name(node, "operator", 8);
        if (node_text(op, bytes) == "/") {
            const auto right = ::ts_node_child_by_field_name(node, "right", 5);
            const auto rhs_text = node_text(right, bytes);
            if (divisor_is_unguarded_length_or_dot(rhs_text)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "division by length(...) / dot(...) without epsilon guard -- "
                    "produces +Inf or NaN at zero divisors; wrap the divisor in "
                    "max(epsilon, ...) or use the SafeNormalize idiom"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "wrap the divisor in max(1e-6, ...) (or a project-tuned epsilon); "
                    "Inf / NaN propagation through subsequent ops corrupts neighbouring "
                    "pixels in TAA / denoiser pipelines"};
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

class DivWithoutEpsilon : public Rule {
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

std::unique_ptr<Rule> make_div_without_epsilon() {
    return std::make_unique<DivWithoutEpsilon>();
}

}  // namespace hlsl_clippy::rules
