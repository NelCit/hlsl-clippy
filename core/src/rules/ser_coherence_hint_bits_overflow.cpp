// ser-coherence-hint-bits-overflow
//
// Detects `MaybeReorderThread(hint, bits)` where `bits > 16` (or
// `HitObject::MaybeReorderThread` variant where the bits arg > 8). Per
// the SER spec (proposal 0027, Accepted), values above the cap are
// silently truncated, producing incoherent reorder.
//
// Stage: Ast.

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
using util::node_text;

constexpr std::string_view k_rule_id = "ser-coherence-hint-bits-overflow";
constexpr std::string_view k_category = "ser";
constexpr std::uint32_t k_max_bits_traceray = 16U;
constexpr std::uint32_t k_max_bits_hitobject = 8U;

[[nodiscard]] bool parse_uint_literal(std::string_view text, std::uint32_t& out) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == 'u' ||
                             text.back() == 'U')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    std::uint32_t value = 0U;
    if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        for (std::size_t i = 2U; i < text.size(); ++i) {
            const char c = text[i];
            std::uint32_t digit = 0U;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
            value = value * 16U + digit;
        }
    } else {
        for (const char c : text) {
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10U + static_cast<std::uint32_t>(c - '0');
        }
    }
    out = value;
    return true;
}

/// Returns the index of the `bits` argument in the `arguments` field of a
/// `call_expression`, or `~0U` if the call's name doesn't match either
/// `MaybeReorderThread` (free function -- bits is arg index 1, since
/// signature is `(hint, bits)`) or `HitObject::MaybeReorderThread` (bits
/// is arg index 2 for the `(hitObject, hint, bits)` overload, or arg
/// index 2 for the `(hitObject, bits)` overload). To stay tolerant of
/// the various overloads, we apply the cap to the LAST integer literal
/// argument when the call name matches.
[[nodiscard]] std::uint32_t cap_for(std::string_view call_text) noexcept {
    // HitObject::MaybeReorderThread variant has the tighter cap.
    if (call_text.find("HitObject") != std::string_view::npos) {
        return k_max_bits_hitobject;
    }
    return k_max_bits_traceray;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        const bool match = (fn_text == "MaybeReorderThread") ||
                           (fn_text.size() >= std::string_view{"MaybeReorderThread"}.size() &&
                            fn_text.find("MaybeReorderThread") != std::string_view::npos);
        if (match) {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            const auto count = ::ts_node_named_child_count(args);
            if (count >= 2U) {
                // Find the LAST argument that is a number_literal.
                std::uint32_t last_value = 0U;
                bool found = false;
                for (std::uint32_t k = count; k > 0U; --k) {
                    const auto a = ::ts_node_named_child(args, k - 1U);
                    if (node_kind(a) == "number_literal") {
                        if (parse_uint_literal(node_text(a, bytes), last_value)) {
                            found = true;
                            break;
                        }
                    }
                }
                if (found) {
                    const auto cap = cap_for(node_text(node, bytes));
                    if (last_value > cap) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message = std::string{"`MaybeReorderThread(...)` last numeric "
                                                   "arg "} +
                                       std::to_string(last_value) +
                                       " exceeds the spec coherence-hint-bits cap " +
                                       std::to_string(cap) +
                                       " -- the SER scheduler silently truncates the value, "
                                       "producing incoherent reorder";
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SerCoherenceHintBitsOverflow : public Rule {
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

std::unique_ptr<Rule> make_ser_coherence_hint_bits_overflow() {
    return std::make_unique<SerCoherenceHintBitsOverflow>();
}

}  // namespace hlsl_clippy::rules
