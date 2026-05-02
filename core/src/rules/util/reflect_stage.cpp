// Implementation of the entry-point / target-profile helpers declared in
// `reflect_stage.hpp`.
//
// The AST-driven `wave_size_for_entry_point` is the most involved bit. It
// walks the parsed tree looking for a function whose identifier matches
// `ep.name`, then scans the source bytes preceding the function's start byte
// for a `[WaveSize(N)]` or `[WaveSize(min, max)]` attribute. The textual
// scan is the same defensive style used by the
// `loop-attribute-conflict` rule: tree-sitter-hlsl v0.2.0 has known gaps
// around the `[attr]` bracket syntax (see external/treesitter-version.md),
// so reading the attribute out of the source bytes preceding the function
// declaration is more robust than relying on a grammar node.

#include "rules/util/reflect_stage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules::util {

namespace {

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* type = ::ts_node_type(node);
    return type != nullptr ? std::string_view{type} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::size_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::size_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

/// Walk back from `start` to the closest preceding `;`, `}`, `{`, or
/// start-of-file. Returns the byte offset of the position just after that
/// boundary -- the start of any attribute-bearing prefix that may precede
/// the function declaration. Mirrors the helper in
/// `loop_attribute_conflict.cpp`.
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

/// Parse a base-10 unsigned integer at `text[i..]`. Sets `out` and returns the
/// new index on success; returns `i` unchanged on no-digits. Saturates at
/// 1e9 to avoid overflow on adversarial input.
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

/// Scan `prefix` for a `[WaveSize(N)]` or `[WaveSize(min, max)]` attribute.
/// Returns the resolved `(min, max)` pair on the first match. Tolerates
/// whitespace inside the brackets; ignores any other attributes (e.g.
/// `[shader("compute")]`, `[numthreads(...)]`).
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> scan_wavesize(
    std::string_view prefix) noexcept {
    constexpr std::string_view k_wavesize = "WaveSize";
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
        if (!match_keyword(rest, k_wavesize)) {
            ++i;
            continue;
        }
        j += k_wavesize.size();
        j = skip_ws(prefix, j);
        if (j >= prefix.size() || prefix[j] != '(') {
            ++i;
            continue;
        }
        ++j;  // past '('
        j = skip_ws(prefix, j);
        std::uint32_t min_v = 0U;
        const std::size_t after_min = parse_uint(prefix, j, min_v);
        if (after_min == j) {
            // No digits parsed; malformed -- skip this `[`.
            ++i;
            continue;
        }
        j = skip_ws(prefix, after_min);
        if (j < prefix.size() && prefix[j] == ',') {
            ++j;
            j = skip_ws(prefix, j);
            std::uint32_t max_v = 0U;
            const std::size_t after_max = parse_uint(prefix, j, max_v);
            if (after_max == j) {
                ++i;
                continue;
            }
            return std::pair<std::uint32_t, std::uint32_t>{min_v, max_v};
        }
        // Single-arg form; `min == max == N`.
        return std::pair<std::uint32_t, std::uint32_t>{min_v, min_v};
    }
    return std::nullopt;
}

/// Recursively walk the tree looking for an identifier inside a
/// `function_declarator` whose name matches `target_name`. Returns the
/// identifier node so the caller can use its start byte as the anchor for
/// preceding-attribute scans. We anchor on the identifier rather than the
/// enclosing `function_definition` because tree-sitter-hlsl may or may not
/// include the leading `[attr]` brackets inside the function_definition's
/// byte range (this is one of the documented grammar gaps); the identifier
/// position is stable regardless.
///
/// Returns a null `TSNode` if no match is found. The traversal is depth-first,
/// document order.
[[nodiscard]] ::TSNode find_function_identifier(::TSNode node,
                                                std::string_view bytes,
                                                std::string_view target_name) noexcept {
    if (::ts_node_is_null(node)) {
        return node;
    }

    if (node_kind(node) == "function_declarator") {
        // The function name is the `declarator` field pointing at an identifier
        // (or, in some grammar paths, a direct identifier child). Scan both.
        ::TSNode ident = ::ts_node_child_by_field_name(node, "declarator", 10);
        if (::ts_node_is_null(ident) || node_kind(ident) != "identifier") {
            const std::uint32_t cc = ::ts_node_child_count(node);
            for (std::uint32_t k = 0; k < cc; ++k) {
                const ::TSNode ch = ::ts_node_child(node, k);
                if (node_kind(ch) == "identifier") {
                    ident = ch;
                    break;
                }
            }
        }
        if (!::ts_node_is_null(ident) && node_kind(ident) == "identifier" &&
            node_text(ident, bytes) == target_name) {
            return ident;
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(node, i);
        const ::TSNode hit = find_function_identifier(child, bytes, target_name);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

}  // namespace

const EntryPointInfo* find_entry_point(const ReflectionInfo& reflection,
                                       std::string_view entry_name) noexcept {
    return reflection.find_entry_point_by_name(entry_name);
}

bool is_pixel_shader(const EntryPointInfo& ep) noexcept {
    return ep.stage == "pixel";
}

bool is_vertex_shader(const EntryPointInfo& ep) noexcept {
    return ep.stage == "vertex";
}

bool is_compute_shader(const EntryPointInfo& ep) noexcept {
    return ep.stage == "compute";
}

bool is_mesh_or_amp_shader(const EntryPointInfo& ep) noexcept {
    return ep.stage == "mesh" || ep.stage == "amplification";
}

bool is_raytracing_shader(const EntryPointInfo& ep) noexcept {
    return ep.stage == "raygeneration" || ep.stage == "intersection" || ep.stage == "anyhit" ||
           ep.stage == "closesthit" || ep.stage == "miss" || ep.stage == "callable";
}

std::optional<std::uint32_t> shader_model_minor(std::string_view target_profile) noexcept {
    // Accept `<prefix>_6_<minor>` where `<prefix>` is `sm` or any 2-letter
    // stage tag (`vs`, `ps`, `cs`, `gs`, `hs`, `ds`, `ms`, `as`, `lib`). We
    // do not validate the prefix tightly -- we just locate the trailing
    // `_6_<digits>` pattern and parse the digits.
    if (target_profile.size() < 5) {
        return std::nullopt;
    }
    // Find the last underscore.
    const auto last_us = target_profile.find_last_of('_');
    if (last_us == std::string_view::npos || last_us + 1 >= target_profile.size()) {
        return std::nullopt;
    }
    // The character before the last underscore must be a digit '6' (we only
    // model SM 6 today; ADR 0010 covers SM 6.7..6.9).
    if (last_us < 1 || target_profile[last_us - 1] != '6') {
        return std::nullopt;
    }
    // The character before that must be an underscore (`_6_<minor>`).
    if (last_us < 2 || target_profile[last_us - 2] != '_') {
        return std::nullopt;
    }
    // Parse the minor digits.
    std::uint32_t minor = 0U;
    bool any = false;
    for (std::size_t i = last_us + 1; i < target_profile.size(); ++i) {
        const char c = target_profile[i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        minor = minor * 10U + static_cast<std::uint32_t>(c - '0');
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return minor;
}

bool target_supports_sm(std::string_view target_profile, std::uint32_t required_minor) noexcept {
    const auto minor = shader_model_minor(target_profile);
    if (!minor.has_value()) {
        return false;
    }
    return *minor >= required_minor;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> wave_size_for_entry_point(
    const AstTree& tree, const EntryPointInfo& ep) noexcept {
    // ADR 0020 sub-phase A v1.3.1 — `.slang` sources reach this helper via
    // reflection-stage rules even though no tree-sitter parse ran. Bail
    // quietly when the AST is unavailable.
    if (tree.raw_tree() == nullptr) {
        return std::nullopt;
    }
    const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
    if (::ts_node_is_null(root)) {
        return std::nullopt;
    }
    const std::string_view bytes = tree.source_bytes();
    const ::TSNode ident = find_function_identifier(root, bytes, ep.name);
    if (::ts_node_is_null(ident)) {
        return std::nullopt;
    }
    const auto ident_lo = static_cast<std::size_t>(::ts_node_start_byte(ident));
    const std::size_t pref_lo = prefix_start(bytes, ident_lo);
    if (pref_lo >= ident_lo) {
        return std::nullopt;
    }
    const std::string_view prefix = bytes.substr(pref_lo, ident_lo - pref_lo);
    return scan_wavesize(prefix);
}

}  // namespace hlsl_clippy::rules::util
