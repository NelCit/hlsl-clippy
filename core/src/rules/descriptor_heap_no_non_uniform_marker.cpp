// descriptor-heap-no-non-uniform-marker
//
// Detects `ResourceDescriptorHeap[i]` / `SamplerDescriptorHeap[i]` accesses
// (SM 6.6+) where the index is not a literal and is not wrapped in
// `NonUniformResourceIndex(...)`. Without the marker the driver may broadcast
// lane 0's descriptor across the wave, producing UB-tinted artefacts.
//
// Detection plan: scan the source bytes for the two literal heap names; for
// every occurrence followed by `[expr]`, check whether `expr` is an integer
// literal or a `NonUniformResourceIndex(...)` call. Otherwise emit. Pure AST
// stage -- the heap names are intrinsic, no reflection lookup required.

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

using util::is_id_char;

constexpr std::string_view k_rule_id = "descriptor-heap-no-non-uniform-marker";
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

void scan_for_heap(const AstTree& tree,
                   std::string_view bytes,
                   std::string_view heap_name,
                   RuleContext& ctx) {
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(heap_name, pos);
        if (found == std::string_view::npos)
            return;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + heap_name.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        std::size_t i = end;
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        if (i >= bytes.size() || bytes[i] != '[') {
            pos = found + 1U;
            continue;
        }
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
        diag.message = std::string{"`"} + std::string{heap_name} +
                       "[...]` index is not wrapped in `NonUniformResourceIndex(...)` -- "
                       "SM 6.6 dynamic-resource access requires the marker when the index can "
                       "vary across lanes (otherwise the driver may broadcast lane 0's "
                       "descriptor)";

        // Wrap the captured index expression in `NonUniformResourceIndex(...)`.
        // The wrap evaluates the index exactly once (no duplication), so this
        // is safe even when the index is a non-trivial expression with side
        // effects. Marked suggestion-grade because the doc page notes that
        // some call sites may already be uniform, where wrapping is harmless
        // but may slow the uniform path slightly on some drivers.
        const auto trimmed = trim(inside);
        if (!trimmed.empty()) {
            // Replace the exact `[...]` content (between `[` and `]`) so
            // any surrounding whitespace inside the brackets is preserved
            // by the wrap.
            const std::uint32_t inside_lo = static_cast<std::uint32_t>(i + 1U);
            const std::uint32_t inside_hi = static_cast<std::uint32_t>(j);
            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "wrap descriptor-heap index in `NonUniformResourceIndex(...)`; "
                "verify the index is actually divergent at this call site"};
            TextEdit edit;
            edit.span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{inside_lo, inside_hi},
            };
            std::string replacement;
            replacement.reserve(trimmed.size() + 28U);
            replacement.append("NonUniformResourceIndex(");
            replacement.append(trimmed);
            replacement.append(")");
            edit.replacement = std::move(replacement);
            fix.edits.push_back(std::move(edit));
            diag.fixes.push_back(std::move(fix));
        }

        ctx.emit(std::move(diag));
        pos = j + 1U;
    }
}

class DescriptorHeapNoNonUniformMarker : public Rule {
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
        scan_for_heap(tree, bytes, "ResourceDescriptorHeap", ctx);
        scan_for_heap(tree, bytes, "SamplerDescriptorHeap", ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_descriptor_heap_no_non_uniform_marker() {
    return std::make_unique<DescriptorHeapNoNonUniformMarker>();
}

}  // namespace shader_clippy::rules
