// wavereadlaneat-constant-non-zero-portability
//
// Detects `WaveReadLaneAt(x, K)` with constant non-zero `K` inside an entry
// point that does not pin the wave size via `[WaveSize(N)]`. The lane index
// `K` may be in range on wave64 (RDNA in wave64 mode) but out of range on
// wave32 (Turing/Ada/RDNA-in-wave32). Surfacing this lets the developer
// either pin the wave size or guard the lane index against
// `WaveGetLaneCount()`.
//
// Detection (Reflection-stage):
//   1. For each entry point in reflection, confirm
//      `wave_size_for_entry_point(...)` is `nullopt` (no `[WaveSize]`).
//   2. Walk the AST under the function body looking for `call_expression`
//      nodes whose function identifier is `WaveReadLaneAt` with exactly two
//      arguments where the second argument is a `number_literal` whose value
//      is a non-zero unsigned integer.
//   3. Emit one suggestion-grade diagnostic per call site. No fix -- the
//      developer must decide whether to pin `[WaveSize]` or to guard the
//      lane index dynamically.
//
// The companion `wavereadlaneat-constant-zero-to-readfirst` rule handles the
// `K == 0` case: prefer `WaveReadLaneFirst`. This rule deliberately does
// NOT fire on `K == 0` (which is portable across every wave size).
//
// Stage: Reflection (uses `wave_size_for_entry_point` to confirm wave-size
// is unpinned).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_stage.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "wavereadlaneat-constant-non-zero-portability";
constexpr std::string_view k_category = "control-flow";
constexpr std::string_view k_intrinsic_name = "WaveReadLaneAt";

[[nodiscard]] bool is_int_suffix(char c) noexcept {
    return c == 'u' || c == 'U' || c == 'l' || c == 'L';
}

/// True iff `text` represents a non-zero unsigned integer literal.
/// Accepts decimal, hex (`0x...`), and an optional integer suffix
/// (`u`/`U`/`l`/`L`). Floating-point literals and zero return false.
[[nodiscard]] bool literal_is_non_zero_integer(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (text[i] == '+') {
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    bool any_digit = false;
    bool saw_non_zero = false;

    // Hex form: 0x... or 0X...
    if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2;
        while (i < text.size()) {
            const char c = text[i];
            const bool is_hex_digit =
                (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!is_hex_digit) {
                break;
            }
            any_digit = true;
            if (c != '0') {
                saw_non_zero = true;
            }
            ++i;
        }
    } else {
        // Decimal form. Reject if any '.', 'e', or 'E' appears.
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            any_digit = true;
            if (text[i] != '0') {
                saw_non_zero = true;
            }
            ++i;
        }
        if (i < text.size() && (text[i] == '.' || text[i] == 'e' || text[i] == 'E')) {
            return false;
        }
    }

    if (!any_digit) {
        return false;
    }

    // Trailing integer suffix only.
    while (i < text.size()) {
        if (!is_int_suffix(text[i])) {
            return false;
        }
        ++i;
    }

    return saw_non_zero;
}

[[nodiscard]] bool contains_identifier(::TSNode node,
                                       std::string_view bytes,
                                       std::string_view target_name) noexcept {
    if (::ts_node_is_null(node)) {
        return false;
    }
    if (node_kind(node) == "identifier" && node_text(node, bytes) == target_name) {
        return true;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (contains_identifier(::ts_node_child(node, i), bytes, target_name)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] ::TSNode find_entry_function(::TSNode root,
                                           std::string_view bytes,
                                           std::string_view entry_name) noexcept {
    if (::ts_node_is_null(root)) {
        return root;
    }
    if (node_kind(root) == "function_definition" && contains_identifier(root, bytes, entry_name)) {
        return root;
    }
    const std::uint32_t count = ::ts_node_child_count(root);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode hit = find_entry_function(::ts_node_child(root, i), bytes, entry_name);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

void scan_for_lane_at(::TSNode node,
                      std::string_view bytes,
                      const AstTree& tree,
                      std::string_view entry_name,
                      RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_intrinsic_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
                const ::TSNode lane = ::ts_node_named_child(args, 1);
                if (node_kind(lane) == "number_literal" &&
                    literal_is_non_zero_integer(node_text(lane, bytes))) {
                    const auto lane_text = node_text(lane, bytes);
                    const auto call_range = tree.byte_range(node);

                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                    diag.message = std::string{"`WaveReadLaneAt(x, "} + std::string{lane_text} +
                                   std::string{")` uses constant lane index "} +
                                   std::string{lane_text} + std::string{" but entry point `"} +
                                   std::string{entry_name} +
                                   std::string{
                                       "` does not pin the wave size via `[WaveSize]` -- the index "
                                       "may be valid on wave64 (RDNA) but out-of-range on wave32 "
                                       "(Turing/Ada/RDNA-in-wave32) and on Xe-HPG wave8/16/32"};
                    // Suggestion-grade: no fix.
                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_for_lane_at(::ts_node_child(node, i), bytes, tree, entry_name, ctx);
    }
}

class WaveReadLaneAtConstantNonZeroPortability : public Rule {
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
        // ADR 0020 sub-phase A v1.3.1 — needs the AST to find WaveReadLaneAt
        // call sites. Bail silently when no tree is available (`.slang` until
        // sub-phase B).
        if (tree.raw_tree() == nullptr) {
            return;
        }
        const std::string_view bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        if (::ts_node_is_null(root)) {
            return;
        }

        for (const auto& ep : reflection.entry_points) {
            // [WaveSize] pinned -- skip; the lane index is provably in range.
            if (util::wave_size_for_entry_point(tree, ep).has_value()) {
                continue;
            }
            const ::TSNode fn = find_entry_function(root, bytes, ep.name);
            if (::ts_node_is_null(fn)) {
                continue;
            }
            scan_for_lane_at(fn, bytes, tree, ep.name, ctx);
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wavereadlaneat_constant_non_zero_portability() {
    return std::make_unique<WaveReadLaneAtConstantNonZeroPortability>();
}

}  // namespace shader_clippy::rules
