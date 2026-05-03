// Implementation of the SM 6.10 utility helpers declared in `sm6_10.hpp`.
//
// `target_is_sm610_or_later` reuses the `shader_model_minor` parser from
// `reflect_stage.cpp` rather than re-implementing one here -- the profile
// grammar is identical (`<prefix>_6_<minor>`) so we lean on the existing
// helper and additionally tolerate the `-preview` suffix that DXC emits for
// the SM 6.10 Agility SDK preview.
//
// `parse_groupshared_limit_attribute` mirrors `wave_size_for_entry_point`
// in `reflect_stage.cpp`: the same defensive textual prefix scan because
// tree-sitter-hlsl v0.2.0 has known gaps around the `[attr]` bracket
// syntax (see external/treesitter-version.md).

#include "rules/util/sm6_10.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <tree_sitter/api.h>

#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_stage.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules::util {

namespace {

/// Strip the `-preview` suffix from a target profile, if present. The DXC
/// SM 6.10 Agility SDK preview emits target strings like `sm_6_10-preview`;
/// `shader_model_minor` would refuse those because of the trailing
/// non-digit characters. Stripping the suffix first lets the parser accept
/// the underlying SM tag.
[[nodiscard]] std::string_view strip_preview_suffix(std::string_view profile) noexcept {
    constexpr std::string_view k_preview = "-preview";
    if (profile.size() >= k_preview.size() &&
        profile.substr(profile.size() - k_preview.size()) == k_preview) {
        return profile.substr(0, profile.size() - k_preview.size());
    }
    return profile;
}

[[nodiscard]] std::size_t skip_ws(std::string_view text, std::size_t i) noexcept {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
        ++i;
    }
    return i;
}

[[nodiscard]] bool match_keyword(std::string_view text, std::string_view keyword) noexcept {
    if (text.size() < keyword.size()) {
        return false;
    }
    if (text.substr(0, keyword.size()) != keyword) {
        return false;
    }
    if (text.size() == keyword.size()) {
        return true;
    }
    const char c = text[keyword.size()];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Walk back from `start` to the closest preceding `;`, `}`, `{`, or
/// start-of-file. Returns the byte offset of the position just after that
/// boundary -- the start of any attribute-bearing prefix that may precede
/// the entry-point declaration. Mirrors the helpers in
/// `reflect_stage.cpp` and `loop_attribute_conflict.cpp`.
[[nodiscard]] std::size_t prefix_start(std::string_view bytes, std::size_t start) noexcept {
    std::size_t i = start;
    while (i > 0) {
        const char c = bytes[i - 1];
        if (c == ';' || c == '}' || c == '{') {
            break;
        }
        --i;
    }
    return i;
}

/// Parse a base-10 unsigned integer at `text[i..]`. Sets `out` and returns
/// the new index on success; returns `i` unchanged when no digits are
/// available. Saturates at 1e9 to avoid overflow on adversarial input.
[[nodiscard]] std::size_t parse_uint(std::string_view text,
                                     std::size_t i,
                                     std::uint32_t& out) noexcept {
    std::uint32_t n = 0U;
    bool any = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        const std::uint32_t digit = static_cast<std::uint32_t>(text[i] - '0');
        if (n < 1'000'000'000U) {
            n = n * 10U + digit;
        }
        any = true;
        ++i;
    }
    if (any) {
        out = n;
    }
    return i;
}

/// Scan `prefix` for a `[GroupSharedLimit(<bytes>)]` attribute. Returns
/// the parsed byte count on the first match; tolerates whitespace inside
/// the brackets and ignores any other attributes in the prefix
/// (e.g. `[shader("compute")]`, `[numthreads(...)]`).
[[nodiscard]] std::optional<std::uint32_t> scan_groupshared_limit(
    std::string_view prefix) noexcept {
    constexpr std::string_view k_attr = "GroupSharedLimit";
    std::size_t i = 0;
    while (i < prefix.size()) {
        if (prefix[i] != '[') {
            ++i;
            continue;
        }
        std::size_t j = skip_ws(prefix, i + 1);
        if (j >= prefix.size()) {
            break;
        }
        const auto rest = prefix.substr(j);
        if (!match_keyword(rest, k_attr)) {
            ++i;
            continue;
        }
        j += k_attr.size();
        j = skip_ws(prefix, j);
        if (j >= prefix.size() || prefix[j] != '(') {
            ++i;
            continue;
        }
        ++j;  // past '('
        j = skip_ws(prefix, j);
        std::uint32_t value = 0U;
        const std::size_t after_value = parse_uint(prefix, j, value);
        if (after_value == j) {
            // No digits parsed; malformed -- skip this `[`.
            ++i;
            continue;
        }
        return value;
    }
    return std::nullopt;
}

/// Extract a function-name identifier from a `function_declarator` /
/// `function_definition` node. Returns a null `TSNode` if no identifier
/// is found. Used to anchor the textual prefix scan on a stable byte
/// position.
[[nodiscard]] ::TSNode function_identifier(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return node;
    }

    const auto kind = node_kind(node);

    // For `function_definition`, descend into the declarator field first.
    if (kind == "function_definition") {
        const ::TSNode decl = ::ts_node_child_by_field_name(node, "declarator", 10);
        if (!::ts_node_is_null(decl)) {
            return function_identifier(decl);
        }
    }

    if (kind == "function_declarator") {
        ::TSNode ident = ::ts_node_child_by_field_name(node, "declarator", 10);
        if (!::ts_node_is_null(ident) && node_kind(ident) == "identifier") {
            return ident;
        }
        const std::uint32_t cc = ::ts_node_child_count(node);
        for (std::uint32_t k = 0; k < cc; ++k) {
            const ::TSNode ch = ::ts_node_child(node, k);
            if (node_kind(ch) == "identifier") {
                return ch;
            }
        }
    }

    // Fallback: depth-first scan for the first identifier descendant.
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(node, i);
        if (node_kind(child) == "identifier") {
            return child;
        }
        const ::TSNode hit = function_identifier(child);
        if (!::ts_node_is_null(hit) && node_kind(hit) == "identifier") {
            return hit;
        }
    }
    return ::TSNode{};
}

}  // namespace

bool target_is_sm610_or_later(std::string_view target_profile) noexcept {
    const auto stripped = strip_preview_suffix(target_profile);
    const auto minor = shader_model_minor(stripped);
    if (!minor.has_value()) {
        return false;
    }
    return *minor >= 10U;
}

bool target_is_sm610_or_later(const ReflectionInfo& reflection) noexcept {
    return target_is_sm610_or_later(reflection.target_profile);
}

bool is_linalg_matrix_type(std::string_view type_name) noexcept {
    constexpr std::string_view k_prefix = "linalg::Matrix";
    // Skip leading whitespace.
    std::size_t i = skip_ws(type_name, 0);
    if (i >= type_name.size()) {
        return false;
    }
    const auto rest = type_name.substr(i);
    if (rest.size() < k_prefix.size() || rest.substr(0, k_prefix.size()) != k_prefix) {
        return false;
    }
    // After the prefix we must see (after optional whitespace) a `<`. This
    // both rejects bare `linalg::MatrixFoo` and confirms the template-arg
    // shape the SM 6.10 spec requires (`linalg::Matrix<T, R, C>`).
    std::size_t j = i + k_prefix.size();
    j = skip_ws(type_name, j);
    if (j >= type_name.size() || type_name[j] != '<') {
        return false;
    }
    return true;
}

std::optional<std::uint32_t> parse_groupshared_limit_attribute(const AstTree& tree,
                                                               ::TSNode entry_point) noexcept {
    if (::ts_node_is_null(entry_point)) {
        return std::nullopt;
    }
    const ::TSNode ident = function_identifier(entry_point);
    if (::ts_node_is_null(ident)) {
        return std::nullopt;
    }
    const std::string_view bytes = tree.source_bytes();
    const auto ident_lo = static_cast<std::size_t>(::ts_node_start_byte(ident));
    if (ident_lo > bytes.size()) {
        return std::nullopt;
    }
    const std::size_t pref_lo = prefix_start(bytes, ident_lo);
    if (pref_lo >= ident_lo) {
        return std::nullopt;
    }
    const std::string_view prefix = bytes.substr(pref_lo, ident_lo - pref_lo);
    return scan_groupshared_limit(prefix);
}

std::uint32_t expected_wave_size_for_target(std::string_view target_profile) noexcept {
    // Empty / unknown profile: pick the modern default (32). The historical
    // SM <= 6.4 default was wave64 (AMD GCN / RDNA1 in wave64), but every
    // modern IHV (RDNA2+, NVIDIA, Intel Xe2) reports a wave width of 32 by
    // default; rules that reason about wave alignment on dispatched grids
    // get conservative behaviour by treating empty as 32 (multiples of 32
    // also satisfy 64-aligned dispatches).
    if (target_profile.empty()) {
        return 32U;
    }
    const auto stripped = strip_preview_suffix(target_profile);
    const auto minor = shader_model_minor(stripped);
    if (!minor.has_value()) {
        return 32U;
    }
    // Wave/Quad intrinsics standardised at SM 6.5; the `[WaveSize]`
    // attribute landed in SM 6.6. For SM 6.5+ we report wave32 as the
    // smallest portable size; for older profiles we fall back to wave64
    // (the historical AMD GCN default).
    return (*minor >= 5U) ? 32U : 64U;
}

}  // namespace shader_clippy::rules::util
