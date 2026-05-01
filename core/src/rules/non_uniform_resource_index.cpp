// non-uniform-resource-index
//
// Detects dynamic indexing into a resource-array binding (`Texture2D
// arr[]`, `ConstantBuffer<T> arr[N]`, etc.) where the index expression is
// not a literal, is not wrapped in `NonUniformResourceIndex(...)`, and is
// therefore at risk of being silently lane-divergent. The DXIL spec makes
// missing-marker access UB.
//
// Detection plan: for every reflection binding whose `array_size` is set
// (bounded array) or absent on a name that the AST shows declared with an
// unbounded `[]`, scan the source bytes for `<name>[expr]`. If `expr` is a
// literal integer (`<name>[3]`) or already starts with
// `NonUniformResourceIndex` token, skip. Otherwise emit. We intentionally do
// not try to prove uniformity from the AST -- that lives in the Phase 4 CFG /
// uniformity oracle.

#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

constexpr std::string_view k_rule_id = "non-uniform-resource-index";
constexpr std::string_view k_category = "bindings";
constexpr std::string_view k_marker = "NonUniformResourceIndex";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool is_integer_literal(std::string_view s) noexcept {
    s = trim(s);
    if (s.empty())
        return false;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return false;
    }
    return true;
}

[[nodiscard]] bool starts_with_marker(std::string_view s) noexcept {
    s = trim(s);
    return s.size() >= k_marker.size() && s.substr(0, k_marker.size()) == k_marker &&
           (s.size() == k_marker.size() || !is_id_char(s[k_marker.size()]));
}

class NonUniformResourceIndex : public Rule {
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
        const auto bytes = tree.source_bytes();
        for (const auto& binding : reflection.bindings) {
            if (binding.name.empty())
                continue;
            // We fire only on resources that look like arrays. The bridge
            // surfaces `array_size` for bounded arrays; unbounded arrays are
            // detected by scanning the declaration text for a `[]` suffix on
            // the binding name.
            const bool is_bounded_array = binding.array_size.has_value();
            const std::uint32_t decl_lo = binding.declaration_span.bytes.lo;
            const std::uint32_t decl_hi = binding.declaration_span.bytes.hi;
            const auto decl_text =
                (decl_lo < bytes.size() && decl_hi <= bytes.size() && decl_hi >= decl_lo)
                    ? bytes.substr(decl_lo, decl_hi - decl_lo)
                    : std::string_view{};
            const bool is_unbounded_array =
                !is_bounded_array && decl_text.find("[]") != std::string_view::npos;
            if (!is_bounded_array && !is_unbounded_array)
                continue;

            const auto& name = binding.name;
            std::size_t pos = 0U;
            while (pos <= bytes.size()) {
                const auto found = bytes.find(name, pos);
                if (found == std::string_view::npos)
                    break;
                const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
                const std::size_t end = found + name.size();
                const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
                if (!ok_left || !ok_right) {
                    pos = found + 1U;
                    continue;
                }
                const auto abs = static_cast<std::uint32_t>(found);
                if (abs >= decl_lo && abs < decl_hi) {
                    pos = found + 1U;
                    continue;
                }
                // Look for `[expr]`.
                std::size_t i = end;
                while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
                    ++i;
                if (i >= bytes.size() || bytes[i] != '[') {
                    pos = found + 1U;
                    continue;
                }
                // Find the matching `]`.
                int depth = 0;
                std::size_t j = i;
                while (j < bytes.size()) {
                    if (bytes[j] == '[')
                        ++depth;
                    else if (bytes[j] == ']') {
                        --depth;
                        if (depth == 0)
                            break;
                    }
                    ++j;
                }
                if (j >= bytes.size()) {
                    pos = found + 1U;
                    continue;
                }
                const auto inside = bytes.substr(i + 1U, j - i - 1U);
                if (is_integer_literal(inside) || starts_with_marker(inside)) {
                    pos = j + 1U;
                    continue;
                }
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                           static_cast<std::uint32_t>(j + 1U)}};
                diag.message =
                    std::string{"resource-array index into `"} + name +
                    "` is not wrapped in `NonUniformResourceIndex(...)` -- if the index can vary "
                    "per lane the DXIL spec calls this UB and the driver may broadcast lane 0's "
                    "descriptor across the wave";
                ctx.emit(std::move(diag));
                pos = j + 1U;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_non_uniform_resource_index() {
    return std::make_unique<NonUniformResourceIndex>();
}

}  // namespace hlsl_clippy::rules
