// groupshared-too-large
//
// Detects total `groupshared` byte size in a translation unit that exceeds a
// configurable threshold (default 16 KB). Large LDS allocations cap CU/SM
// occupancy at 1 thread group on RDNA 3 and Turing/Ada, eliminating the
// hardware's wave-level latency hiding.
//
// Detection plan: AST. Walk the source for every `groupshared` declaration,
// extract the type and (optional) array size, and sum. Emit at the first
// `groupshared` declaration when the total exceeds the threshold.

#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "groupshared-too-large";
constexpr std::string_view k_category = "workgroup";
constexpr std::uint32_t k_threshold_bytes = 16384U;

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] std::uint32_t sizeof_scalar(std::string_view t) noexcept {
    auto component_count = [&]([[maybe_unused]] std::string_view base,
                               std::size_t prefix_len) -> std::uint32_t {
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
        const auto v = component_count("float", 5U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("uint") && !t.starts_with("uint64")) {
        const auto v = component_count("uint", 4U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("int") && !t.starts_with("int64")) {
        const auto v = component_count("int", 3U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("bool")) {
        const auto v = component_count("bool", 4U);
        return v == 0U ? 0U : 4U * v;
    }
    if (t.starts_with("half")) {
        const auto v = component_count("half", 4U);
        return v == 0U ? 0U : 2U * v;
    }
    if (t.starts_with("double")) {
        const auto v = component_count("double", 6U);
        return v == 0U ? 0U : 8U * v;
    }
    return 0U;
}

class GroupsharedTooLarge : public Rule {
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
        constexpr std::string_view k_kw = "groupshared";
        std::uint32_t total = 0U;
        std::uint32_t first_lo = 0U;
        std::uint32_t first_hi = 0U;
        bool any = false;
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_kw, pos);
            if (found == std::string_view::npos)
                break;
            const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
            const std::size_t end = found + k_kw.size();
            const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
            if (!ok_left || !ok_right) {
                pos = found + 1U;
                continue;
            }
            // Skip trailing qualifiers + whitespace and read the type token.
            std::size_t i = end;
            while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
                ++i;
            // Skip 'volatile' if present.
            if (bytes.size() - i >= 8U && bytes.substr(i, 8U) == "volatile") {
                i += 8U;
                while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
                    ++i;
            }
            const std::size_t type_lo = i;
            while (i < bytes.size() && (is_id_char(bytes[i]) || bytes[i] == 'x'))
                ++i;
            const auto type_text = bytes.substr(type_lo, i - type_lo);
            // Skip whitespace + name.
            while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
                ++i;
            while (i < bytes.size() && is_id_char(bytes[i]))
                ++i;
            // Optional array suffix `[N]` or `[N][M]` etc.
            std::uint32_t element_count = 1U;
            while (i < bytes.size() && bytes[i] == '[') {
                ++i;
                std::size_t inside_start = i;
                while (i < bytes.size() && bytes[i] != ']')
                    ++i;
                if (i >= bytes.size())
                    break;
                const auto digits = trim(bytes.substr(inside_start, i - inside_start));
                std::uint32_t v = 0U;
                bool ok = !digits.empty();
                for (const char c : digits) {
                    // Accept simple integer literals.
                    if (c >= '0' && c <= '9') {
                        v = v * 10U + static_cast<std::uint32_t>(c - '0');
                    } else {
                        ok = false;
                        break;
                    }
                }
                if (!ok) {
                    element_count = 0U;
                    break;
                }
                element_count *= v;
                ++i;
            }
            const std::uint32_t per_elem = sizeof_scalar(type_text);
            if (per_elem != 0U && element_count != 0U) {
                total += per_elem * element_count;
            }
            if (!any) {
                any = true;
                first_lo = static_cast<std::uint32_t>(found);
                first_hi = static_cast<std::uint32_t>(i);
            }
            pos = i;
        }
        if (!any || total <= k_threshold_bytes)
            return;
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = ByteSpan{first_lo, first_hi}};
        diag.message = std::string{"total `groupshared` allocation is "} + std::to_string(total) +
                       " bytes (> " + std::to_string(k_threshold_bytes) +
                       "-byte threshold) -- LDS pressure caps CU/SM occupancy at one thread "
                       "group, eliminating wave-level latency hiding";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_too_large() {
    return std::make_unique<GroupsharedTooLarge>();
}

}  // namespace hlsl_clippy::rules
