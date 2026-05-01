// coopvec-non-uniform-matrix-handle
//
// Detects a cooperative-vector matmul whose matrix-handle / offset / stride /
// interpretation argument is wave-divergent. Non-uniform arguments either
// serialise the matmul across the wave (perf) or, on stricter
// implementations, produce undefined behaviour.
//
// Stage: Ast (forward-compatible-stub for Phase 4 ControlFlow / uniformity
// analysis).
//
// The full rule needs the Phase 4 uniformity oracle (`UniformityOracle::
// of_expr`) on each call argument. That infrastructure is shipped, but
// running the analysis here would tie this rule to Stage::ControlFlow and
// move it to a different rule pack. The Phase 3 stub fires on the trivial
// trigger: a matmul whose argument list contains an `SV_DispatchThreadID` or
// `SV_GroupThreadID` reference -- the canonical divergent-source identifiers
// for compute shaders. The Phase 4 rule will replace this with a precise
// uniformity check.

#include <array>
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

constexpr std::string_view k_rule_id = "coopvec-non-uniform-matrix-handle";
constexpr std::string_view k_category = "cooperative-vector";

constexpr std::array<std::string_view, 3> k_calls{
    "MatrixVectorMul",
    "MatrixMul",
    "OuterProductAccumulate",
};

constexpr std::array<std::string_view, 4> k_divergent_sources{
    "SV_DispatchThreadID",
    "SV_GroupThreadID",
    "WaveGetLaneIndex",
    "GetWaveLaneIndex",
};

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
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

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto call_text = node_text(node, bytes);
        bool is_target_call = false;
        for (const auto name : k_calls) {
            const auto pos = call_text.find(name);
            if (pos != std::string_view::npos && pos < call_text.find('(')) {
                is_target_call = true;
                break;
            }
        }
        if (is_target_call) {
            for (const auto src : k_divergent_sources) {
                if (call_text.find(src) != std::string_view::npos) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{"cooperative-vector matmul argument depends on `"} +
                                   std::string{src} + "` (wave-divergent); per-lane " +
                                   "matrix arguments serialise the matmul across the " +
                                   "wave -- hoist to a uniform value (proposal 0029)";
                    ctx.emit(std::move(diag));
                    break;
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CoopvecNonUniformMatrixHandle : public Rule {
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

std::unique_ptr<Rule> make_coopvec_non_uniform_matrix_handle() {
    return std::make_unique<CoopvecNonUniformMatrixHandle>();
}

}  // namespace hlsl_clippy::rules
