// excess-interpolators
//
// Detects vertex / domain / geometry / mesh-shader output structs whose
// total interpolator slot count exceeds the hardware budget (16 four-component
// slots on D3D12, matching the SM6 spec). Slot exhaustion forces the driver
// to spill into per-vertex memory, which costs bandwidth on every primitive
// reassembly.
//
// Detection plan: AST. For every struct that contains at least one
// `TEXCOORDn` / `COLORn` / `BLENDINDICESn` semantic, sum the components used.
// A `float4` field uses 4 components; `float3` uses 3 (rounded up to 4 in
// the slot model); each scalar uses 1. Emit when the total exceeds 64
// (16 slots * 4 components).

#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "excess-interpolators";
constexpr std::string_view k_category = "bindings";
constexpr std::uint32_t k_slot_count = 16U;
constexpr std::uint32_t k_components_per_slot = 4U;
constexpr std::uint32_t k_threshold_components = k_slot_count * k_components_per_slot;

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] std::uint32_t component_count_of(std::string_view t) noexcept {
    auto suffix_count = [&](std::size_t prefix_len) noexcept -> std::uint32_t {
        if (t.size() <= prefix_len)
            return 1U;
        const char c = t[prefix_len];
        if (c >= '1' && c <= '4')
            return static_cast<std::uint32_t>(c - '0');
        return 1U;
    };
    if (t.starts_with("float"))
        return suffix_count(5U);
    if (t.starts_with("uint") && !t.starts_with("uint64"))
        return suffix_count(4U);
    if (t.starts_with("int") && !t.starts_with("int64"))
        return suffix_count(3U);
    if (t.starts_with("half"))
        return suffix_count(4U);
    if (t.starts_with("bool"))
        return suffix_count(4U);
    if (t.starts_with("min16float"))
        return suffix_count(10U);
    return 1U;
}

[[nodiscard]] bool is_interpolator_semantic(std::string_view sem) noexcept {
    sem = trim(sem);
    return sem.starts_with("TEXCOORD") || sem.starts_with("COLOR") ||
           sem.starts_with("BLENDINDICES") || sem.starts_with("BLENDWEIGHT") ||
           sem.starts_with("NORMAL") || sem.starts_with("TANGENT") || sem.starts_with("BINORMAL");
}

class ExcessInterpolators : public Rule {
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
        const auto bytes = tree.source_bytes();
        // Walk every `struct <Name> { ... };` block.
        constexpr std::string_view k_kw = "struct";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_kw, pos);
            if (found == std::string_view::npos)
                return;
            const std::size_t after = found + k_kw.size();
            // Skip if `struct` is part of a larger identifier.
            const bool ok_left =
                (found == 0) ||
                !((bytes[found - 1] >= 'a' && bytes[found - 1] <= 'z') ||
                  (bytes[found - 1] >= 'A' && bytes[found - 1] <= 'Z') ||
                  (bytes[found - 1] >= '0' && bytes[found - 1] <= '9') || bytes[found - 1] == '_');
            const bool ok_right =
                after < bytes.size() &&
                (bytes[after] == ' ' || bytes[after] == '\t' || bytes[after] == '\n');
            if (!ok_left || !ok_right) {
                pos = found + 1U;
                continue;
            }
            const auto lb = bytes.find('{', after);
            const auto rb_search_lo = (lb != std::string_view::npos) ? lb : after;
            const auto rb = bytes.find('}', rb_search_lo);
            if (lb == std::string_view::npos || rb == std::string_view::npos || rb <= lb + 1) {
                pos = after;
                continue;
            }
            const auto body = bytes.substr(lb + 1U, rb - lb - 1U);
            // Walk fields by `;`.
            std::uint32_t total_components = 0U;
            std::size_t i = 0U;
            bool any_interp = false;
            while (i < body.size()) {
                const std::size_t start = i;
                while (i < body.size() && body[i] != ';')
                    ++i;
                if (i >= body.size())
                    break;
                const auto field = trim(body.substr(start, i - start));
                ++i;
                if (field.empty())
                    continue;
                // Field shape: `<type> <name> : <semantic>;`
                const auto colon = field.find(':');
                if (colon == std::string_view::npos)
                    continue;
                const auto sem = trim(field.substr(colon + 1U));
                if (!is_interpolator_semantic(sem))
                    continue;
                const auto lhs = trim(field.substr(0, colon));
                const auto sp = lhs.find(' ');
                if (sp == std::string_view::npos)
                    continue;
                const auto type_part = trim(lhs.substr(0, sp));
                total_components += component_count_of(type_part);
                any_interp = true;
            }
            if (any_interp && total_components > k_threshold_components) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                           static_cast<std::uint32_t>(rb + 1U)}};
                diag.message = std::string{"struct interpolator components total "} +
                               std::to_string(total_components) + " (> " +
                               std::to_string(k_threshold_components) +
                               " hardware budget) -- the driver spills excess interpolators to "
                               "per-vertex memory and pays the bandwidth on every primitive "
                               "reassembly";
                ctx.emit(std::move(diag));
            }
            pos = rb + 1U;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_excess_interpolators() {
    return std::make_unique<ExcessInterpolators>();
}

}  // namespace hlsl_clippy::rules
