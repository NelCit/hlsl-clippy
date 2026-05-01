// static-sampler-when-dynamic-used
//
// Heuristic: a sampler binding that is declared once at file scope and used
// only inside one entry point is a strong candidate for promotion to a D3D12
// static sampler. Static samplers cost no descriptor heap slot and are
// pre-resident on every IHV, so a sampler whose state never varies across
// draws should be declared statically in the root signature rather than
// dynamically bound per-draw.
//
// Detection (Reflection stage):
//   1. Walk the reflection bindings; for each `SamplerState` /
//      `SamplerComparisonState`, count how many distinct entry points
//      reference the sampler's identifier in their function bodies.
//   2. If the sampler is referenced by exactly one entry point AND the
//      reflection result also reports at least one entry point overall, emit
//      a suggestion-grade diagnostic anchored at the sampler binding's
//      `declaration_span`.
//
// The rule is suggestion-grade with no machine-applicable fix: the actual
// promotion lives in the application's root-signature definition, which the
// linter does not own.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"
#include "rules/util/reflect_stage.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "static-sampler-when-dynamic-used";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// True when `text` contains `name` as a complete identifier token (not just
/// a substring of a longer identifier).
[[nodiscard]] bool contains_ident(std::string_view text, std::string_view name) noexcept {
    if (name.empty() || text.size() < name.size()) {
        return false;
    }
    std::size_t pos = 0;
    while (pos <= text.size() - name.size()) {
        const auto found = text.find(name, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0) || is_id_boundary(text[found - 1]);
        const std::size_t end = found + name.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1;
    }
    return false;
}

/// Walk the AST to find the byte range of the function body whose identifier
/// matches `entry_name`. Returns an empty `ByteSpan` when no such function is
/// found (caller must skip).
[[nodiscard]] ByteSpan find_function_body_span(::TSNode node,
                                               std::string_view bytes,
                                               std::string_view entry_name) noexcept {
    if (::ts_node_is_null(node)) {
        return ByteSpan{};
    }
    const char* type = ::ts_node_type(node);
    const std::string_view kind = (type != nullptr) ? std::string_view{type} : std::string_view{};
    if (kind == "function_definition") {
        const ::TSNode declarator = ::ts_node_child_by_field_name(node, "declarator", 10);
        if (!::ts_node_is_null(declarator)) {
            const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(declarator));
            const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(declarator));
            if (lo <= bytes.size() && hi <= bytes.size() && hi >= lo) {
                const auto decl_text = bytes.substr(lo, hi - lo);
                if (contains_ident(decl_text, entry_name)) {
                    const ::TSNode body = ::ts_node_child_by_field_name(node, "body", 4);
                    if (!::ts_node_is_null(body)) {
                        return ByteSpan{
                            static_cast<std::uint32_t>(::ts_node_start_byte(body)),
                            static_cast<std::uint32_t>(::ts_node_end_byte(body)),
                        };
                    }
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto span = find_function_body_span(::ts_node_child(node, i), bytes, entry_name);
        if (span.hi > span.lo) {
            return span;
        }
    }
    return ByteSpan{};
}

class StaticSamplerWhenDynamicUsed : public Rule {
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
        if (reflection.entry_points.empty() || reflection.bindings.empty()) {
            return;
        }
        const auto bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());

        for (const auto& binding : reflection.bindings) {
            if (!util::is_sampler(binding.kind)) {
                continue;
            }
            std::size_t referencing_entry_points = 0;
            for (const auto& ep : reflection.entry_points) {
                const auto body_span = find_function_body_span(root, bytes, ep.name);
                if (body_span.hi <= body_span.lo) {
                    continue;
                }
                const auto body_text = bytes.substr(body_span.lo, body_span.hi - body_span.lo);
                if (contains_ident(body_text, binding.name)) {
                    ++referencing_entry_points;
                    if (referencing_entry_points > 1U) {
                        break;
                    }
                }
            }
            if (referencing_entry_points != 1U) {
                continue;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = binding.declaration_span.bytes,
            };
            diag.message = std::string{"sampler `"} + binding.name +
                           "` is referenced from a single entry point and never varies across "
                           "draws -- consider promoting it to a D3D12 static sampler in the "
                           "root signature (saves a descriptor heap slot, pre-resident on "
                           "every IHV)";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_static_sampler_when_dynamic_used() {
    return std::make_unique<StaticSamplerWhenDynamicUsed>();
}

}  // namespace hlsl_clippy::rules
