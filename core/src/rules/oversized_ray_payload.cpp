// oversized-ray-payload
//
// Detects DXR ray payload / hit-attribute / shader-record structs whose total
// byte size exceeds 32 bytes (the practical register-only ceiling for the
// payload across IHVs). Per the DXR spec the *driver-imposed* ceiling is
// higher (RDNA 2/3 docs cite 64-128 B before spilling to the on-chip ray
// stack) but every microbench (NVIDIA Turing+ / Ada / RDNA 2/3 / Xe-HPG)
// shows latency degrading sharply once the payload spills out of registers.
// 32 bytes is two `float4`s plus a flag word — the practical sweet spot.
//
// Stage: Reflection. We walk the AST handed to `on_reflection` because Slang
// does not currently surface payload structs through `ReflectionInfo.cbuffers`;
// the rule looks for any `struct` declared in a translation unit that also
// contains a `TraceRay` / `ReportHit` / `[shader("closesthit")]` /
// `[shader("anyhit")]` / `[shader("miss")]` / `[shader("raygeneration")]`
// marker, sums the field sizes via the same scalar/vector/matrix sizing
// helper the `groupshared-too-large` rule uses, and emits when the total
// exceeds 32 bytes. ADR 0017 §"Trade-offs" documents this as a heuristic
// upper-bound; the rule ships at warn severity.

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

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "oversized-ray-payload";
constexpr std::string_view k_category = "dxr";
constexpr std::uint32_t k_threshold_bytes = 32U;

[[nodiscard]] std::uint32_t scalar_size(std::string_view t) noexcept {
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
    if (t.starts_with("half") || t.starts_with("min16")) {
        const auto v = component_count(t.starts_with("half") ? 4U : 10U);
        return v == 0U ? 0U : 2U * v;
    }
    if (t.starts_with("double")) {
        const auto v = component_count(6U);
        return v == 0U ? 0U : 8U * v;
    }
    return 0U;
}

[[nodiscard]] std::uint32_t parse_uint(std::string_view s) noexcept {
    std::uint32_t v = 0U;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return 0U;
        v = v * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return v;
}

/// Sum the byte size of every field declaration inside `body_text`. This is
/// a deliberately tolerant scan: any line of the form `<type> <name>[ N ];`
/// (semicolon-terminated) contributes `scalar_size(type) * N` bytes.
[[nodiscard]] std::uint32_t sum_struct_size(std::string_view body_text) noexcept {
    std::uint32_t total = 0U;
    std::size_t pos = 0U;
    while (pos < body_text.size()) {
        // Skip whitespace.
        while (pos < body_text.size() && (body_text[pos] == ' ' || body_text[pos] == '\t' ||
                                          body_text[pos] == '\n' || body_text[pos] == '\r' ||
                                          body_text[pos] == '{' || body_text[pos] == '}')) {
            ++pos;
        }
        // End-of-statement scan.
        const auto semi = body_text.find(';', pos);
        if (semi == std::string_view::npos)
            break;
        // Type token: leading word.
        std::size_t i = pos;
        while (i < semi && is_id_char(body_text[i]))
            ++i;
        const auto type_text = body_text.substr(pos, i - pos);
        const auto stmt = body_text.substr(pos, semi - pos);
        std::uint32_t per = scalar_size(type_text);
        if (per == 0U) {
            pos = semi + 1U;
            continue;
        }
        // Look for an array suffix `[N]`.
        std::uint32_t count = 1U;
        const auto lb = stmt.find('[');
        if (lb != std::string_view::npos) {
            const auto rb = stmt.find(']', lb + 1U);
            if (rb != std::string_view::npos) {
                std::string_view digits = stmt.substr(lb + 1U, rb - lb - 1U);
                while (!digits.empty() && (digits.front() == ' ' || digits.front() == '\t'))
                    digits.remove_prefix(1U);
                while (!digits.empty() && (digits.back() == ' ' || digits.back() == '\t'))
                    digits.remove_suffix(1U);
                const std::uint32_t n = parse_uint(digits);
                if (n != 0U)
                    count = n;
            }
        }
        total += per * count;
        pos = semi + 1U;
    }
    return total;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    if (kind == "struct_specifier") {
        const auto struct_text = node_text(node, bytes);
        if (!struct_text.empty()) {
            const auto open = struct_text.find('{');
            const auto close = struct_text.rfind('}');
            if (open != std::string_view::npos && close != std::string_view::npos &&
                close > open) {
                const auto body = struct_text.substr(open + 1U, close - open - 1U);
                const auto total = sum_struct_size(body);
                if (total > k_threshold_bytes) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message =
                        std::string{"struct used as a DXR ray payload is "} +
                        std::to_string(total) +
                        " bytes (> 32-byte register-only ceiling) -- payload spills to the "
                        "on-chip ray stack on every `TraceRay`, costing latency on every "
                        "leaf-traversal step";
                    ctx.emit(std::move(diag));
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class OversizedRayPayload : public Rule {
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
        const auto bytes = tree.source_bytes();
        // Only fire when the source actually has DXR markers; avoid spurious
        // firings on plain compute / pixel shaders that happen to declare
        // large structs.
        const bool has_dxr_marker =
            bytes.find("TraceRay") != std::string_view::npos ||
            bytes.find("ReportHit") != std::string_view::npos ||
            bytes.find("\"closesthit\"") != std::string_view::npos ||
            bytes.find("\"anyhit\"") != std::string_view::npos ||
            bytes.find("\"raygeneration\"") != std::string_view::npos ||
            bytes.find("\"miss\"") != std::string_view::npos;
        if (!has_dxr_marker)
            return;
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_oversized_ray_payload() {
    return std::make_unique<OversizedRayPayload>();
}

}  // namespace hlsl_clippy::rules
