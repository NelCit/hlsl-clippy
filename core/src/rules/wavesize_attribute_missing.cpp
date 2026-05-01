// wavesize-attribute-missing
//
// Detects an entry point that calls wave intrinsics whose result depends on
// the wave size (e.g. `WaveGetLaneCount`, `WaveActiveBallot`,
// `WaveReadLaneAt`, `WavePrefixSum`, ...) but does not pin the wave size via
// `[WaveSize(N)]` or `[WaveSize(min, max)]`. Without the attribute, the
// driver chooses the wave size: RDNA 1/2/3 may run wave32 or wave64,
// Turing/Ada always wave32, Xe-HPG wave8/16/32. Results that index by lane
// count, ballot mask width, or active-lane count silently change between
// IHVs and drivers.
//
// Detection (Reflection-stage):
//   For each entry point in reflection,
//     - if the function declares `[WaveSize(...)]` (any form) skip;
//     - walk the AST under the function body looking for a `call_expression`
//       whose function identifier begins with `Wave` (any wave intrinsic).
//   On a match, emit a single suggestion-grade diagnostic per entry point
//   anchored at the first wave-intrinsic call site. No fix -- the developer
//   must choose the right wave size for the algorithm.
//
// Stage: Reflection (uses `wave_size_for_entry_point` + entry-point info).

#include <cstddef>
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
#include "rules/util/reflect_stage.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "wavesize-attribute-missing";
constexpr std::string_view k_category = "workgroup";

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

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

/// True when `name` looks like a wave-size-dependent intrinsic. Any HLSL
/// intrinsic prefixed `Wave` is conservatively considered wave-size-dependent
/// for this rule's purposes -- the only common false-positive would be a
/// user-defined function that happens to start with `Wave`.
[[nodiscard]] bool is_wave_intrinsic(std::string_view name) noexcept {
    constexpr std::string_view k_prefix = "Wave";
    return name.size() > k_prefix.size() && name.substr(0, k_prefix.size()) == k_prefix;
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

/// Walk `node` looking for the first `call_expression` whose function name
/// matches `is_wave_intrinsic`. Returns the call node on hit, null otherwise.
[[nodiscard]] ::TSNode find_first_wave_call(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return node;
    }
    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (is_wave_intrinsic(node_text(fn, bytes))) {
            return node;
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode hit = find_first_wave_call(::ts_node_child(node, i), bytes);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

class WaveSizeAttributeMissing : public Rule {
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
        const std::string_view bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        if (::ts_node_is_null(root)) {
            return;
        }

        for (const auto& ep : reflection.entry_points) {
            // Only entry-points that can host wave intrinsics: compute, mesh,
            // amplification, pixel. Vertex/RT shaders have wave intrinsics too
            // but the most common use case is compute -- keep the surface
            // broad to catch misuse on every interesting stage.
            if (!util::is_compute_shader(ep) && !util::is_mesh_or_amp_shader(ep) &&
                !util::is_pixel_shader(ep)) {
                continue;
            }

            // If the function declares any [WaveSize(...)] attribute, skip.
            if (util::wave_size_for_entry_point(tree, ep).has_value()) {
                continue;
            }

            const ::TSNode fn = find_entry_function(root, bytes, ep.name);
            if (::ts_node_is_null(fn)) {
                continue;
            }
            const ::TSNode call = find_first_wave_call(fn, bytes);
            if (::ts_node_is_null(call)) {
                continue;
            }

            const ::TSNode fn_ident = ::ts_node_child_by_field_name(call, "function", 8);
            const auto intrinsic_name = node_text(fn_ident, bytes);

            const auto call_range = tree.byte_range(call);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
            diag.message = std::string{"entry point `"} + ep.name +
                           std::string{"` calls wave intrinsic `"} + std::string{intrinsic_name} +
                           std::string{
                               "` but does not pin the wave size via `[WaveSize(N)]` or "
                               "`[WaveSize(min, max)]` -- results that index by lane count "
                               "silently change between RDNA wave32/wave64, Turing/Ada "
                               "wave32, and Xe-HPG wave8/16/32"};
            // Suggestion-grade: no fix. The developer must choose the wave
            // size that matches the algorithm.
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wavesize_attribute_missing() {
    return std::make_unique<WaveSizeAttributeMissing>();
}

}  // namespace hlsl_clippy::rules
