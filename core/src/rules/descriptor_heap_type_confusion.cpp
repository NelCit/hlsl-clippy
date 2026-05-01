// descriptor-heap-type-confusion
//
// Detects mismatch between the SM 6.6 dynamic-resource heap and the type
// the indexed value is then used as. The rule fires when the source uses
// `SamplerDescriptorHeap[...]` to obtain a non-sampler view (assigning to a
// `Texture2D` / `Buffer` / `ByteAddressBuffer` / etc.) or
// `ResourceDescriptorHeap[...]` to obtain a `SamplerState` /
// `SamplerComparisonState`. Mixing the heaps is a common typo on systems
// that ship one global descriptor heap of each type.
//
// Detection plan: walk the source for assignments / declarations of the form
// `<type> name = <Heap>[...];`. Tokenise the LHS type into one of {sampler,
// non-sampler}. Emit when the LHS class disagrees with the RHS heap.

#include <cctype>
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

constexpr std::string_view k_rule_id = "descriptor-heap-type-confusion";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] bool token_is_sampler_type(std::string_view t) noexcept {
    return t == "SamplerState" || t == "SamplerComparisonState";
}

[[nodiscard]] bool token_is_non_sampler_resource(std::string_view t) noexcept {
    if (t.empty())
        return false;
    if (t == "ConstantBuffer" || t == "Buffer" || t == "RWBuffer" || t == "ByteAddressBuffer" ||
        t == "RWByteAddressBuffer" || t == "StructuredBuffer" || t == "RWStructuredBuffer")
        return true;
    return t.size() >= 7U && t.substr(0, 7U) == "Texture";
}

void scan(const AstTree& tree, std::string_view bytes, RuleContext& ctx) {
    // Look for `= ResourceDescriptorHeap[` and `= SamplerDescriptorHeap[`.
    // Walk back to the previous identifier-like token to read the LHS type.
    auto scan_one = [&](std::string_view heap, bool wants_sampler_lhs) {
        std::size_t pos = 0U;
        while (pos <= bytes.size()) {
            const auto found = bytes.find(heap, pos);
            if (found == std::string_view::npos)
                return;
            const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
            const std::size_t end = found + heap.size();
            const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
            if (!ok_left || !ok_right) {
                pos = found + 1U;
                continue;
            }
            // Walk back from `found` over whitespace, expecting a `=`.
            if (found < 2U) {
                pos = found + 1U;
                continue;
            }
            std::size_t k = found - 1U;
            while (k > 0U && (bytes[k] == ' ' || bytes[k] == '\t'))
                --k;
            if (bytes[k] != '=' || (k > 0U && bytes[k - 1U] == '=') ||
                (k + 1U < bytes.size() && bytes[k + 1U] == '=')) {
                pos = found + 1U;
                continue;
            }
            // Walk back over `name`, then over whitespace, then over the type
            // token.
            if (k == 0U) {
                pos = found + 1U;
                continue;
            }
            std::size_t m = k - 1U;
            while (m > 0U && (bytes[m] == ' ' || bytes[m] == '\t'))
                --m;
            std::size_t name_hi = m + 1U;
            while (m > 0U && is_id_char(bytes[m]))
                --m;
            const std::size_t name_lo = is_id_char(bytes[m]) ? m : m + 1U;
            if (name_lo >= name_hi) {
                pos = found + 1U;
                continue;
            }
            // Skip whitespace before the type.
            std::size_t t = (name_lo == 0U) ? 0U : name_lo - 1U;
            while (t > 0U && (bytes[t] == ' ' || bytes[t] == '\t'))
                --t;
            std::size_t type_hi = t + 1U;
            // Scan back over identifier + optional template args `>`.
            // Simplification: if the previous char is `>`, walk back to the
            // matching `<` then walk back over the identifier.
            if (t < bytes.size() && bytes[t] == '>') {
                int depth = 0;
                while (t > 0U) {
                    if (bytes[t] == '>')
                        ++depth;
                    else if (bytes[t] == '<') {
                        --depth;
                        if (depth == 0) {
                            if (t == 0U)
                                break;
                            --t;
                            break;
                        }
                    }
                    --t;
                }
                while (t > 0U && (bytes[t] == ' ' || bytes[t] == '\t'))
                    --t;
                type_hi = t + 1U;
            }
            while (t > 0U && is_id_char(bytes[t]))
                --t;
            const std::size_t type_lo = is_id_char(bytes[t]) ? t : t + 1U;
            if (type_lo >= type_hi) {
                pos = found + 1U;
                continue;
            }
            const auto type_token = bytes.substr(type_lo, type_hi - type_lo);

            const bool lhs_is_sampler = token_is_sampler_type(type_token);
            const bool lhs_is_non_sampler = token_is_non_sampler_resource(type_token);
            const bool mismatch =
                (wants_sampler_lhs && lhs_is_non_sampler) || (!wants_sampler_lhs && lhs_is_sampler);
            if (mismatch) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                           static_cast<std::uint32_t>(end)}};
                diag.message = std::string{"`"} + std::string{heap} + "[...]` indexed into a `" +
                               std::string{type_token} +
                               "` -- the heap kind disagrees with the LHS resource class; use " +
                               (wants_sampler_lhs ? std::string{"`ResourceDescriptorHeap`"}
                                                  : std::string{"`SamplerDescriptorHeap`"}) +
                               " for that type";
                ctx.emit(std::move(diag));
            }
            pos = end;
        }
    };
    scan_one("SamplerDescriptorHeap", true);
    scan_one("ResourceDescriptorHeap", false);
}

class DescriptorHeapTypeConfusion : public Rule {
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
        scan(tree, tree.source_bytes(), ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_descriptor_heap_type_confusion() {
    return std::make_unique<DescriptorHeapTypeConfusion>();
}

}  // namespace hlsl_clippy::rules
