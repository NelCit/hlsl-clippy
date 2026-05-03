// dispatchmesh-grid-too-small-for-wave
//
// Detects `DispatchMesh(x, y, z)` calls with constant integer literal
// args whose product is less than the target's expected wave size.
// Dispatching less than one wave wastes the entire dispatch -- the
// final wave launches with all-but-N lanes masked off.
//
// Stage: Reflection (uses target profile to determine wave size).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "dispatchmesh-grid-too-small-for-wave";
constexpr std::string_view k_category = "mesh";

[[nodiscard]] bool parse_uint(std::string_view s, std::uint32_t& out) noexcept {
    if (s.empty()) {
        return false;
    }
    std::uint32_t v = 0U;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10U + static_cast<std::uint32_t>(c - '0');
    }
    out = v;
    return true;
}

void walk(::TSNode node,
          std::string_view bytes,
          std::uint32_t wave_size,
          const AstTree& tree,
          RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "DispatchMesh") {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) >= 3U) {
                std::array<std::uint32_t, 3U> dims{};
                bool all_const = true;
                for (std::uint32_t k = 0; k < 3U; ++k) {
                    const auto a = ::ts_node_named_child(args, k);
                    if (node_kind(a) != "number_literal") {
                        all_const = false;
                        break;
                    }
                    const auto txt = node_text(a, bytes);
                    if (!parse_uint(txt, dims[k])) {
                        all_const = false;
                        break;
                    }
                }
                if (all_const) {
                    const std::uint64_t total = static_cast<std::uint64_t>(dims[0]) *
                                                static_cast<std::uint64_t>(dims[1]) *
                                                static_cast<std::uint64_t>(dims[2]);
                    if (total > 0U && total < wave_size) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message =
                            std::string{"`DispatchMesh("} + std::to_string(dims[0]) + ", " +
                            std::to_string(dims[1]) + ", " + std::to_string(dims[2]) + ")` total " +
                            std::to_string(total) + " is less than wave size " +
                            std::to_string(wave_size) +
                            " -- dispatching less than one wave wastes the entire dispatch "
                            "(the lanes beyond the grid still consume a wave slot)";
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, wave_size, tree, ctx);
    }
}

class DispatchMeshGridTooSmallForWave : public Rule {
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
        // Use 32 as the modern-IHV portable wave size (RDNA 2+ / Turing+ /
        // Xe-HPG default). See `expected_wave_size_for_target` in
        // `core/src/rules/util/sm6_10.hpp`.
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), 32U, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_dispatchmesh_grid_too_small_for_wave() {
    return std::make_unique<DispatchMeshGridTooSmallForWave>();
}

}  // namespace shader_clippy::rules
