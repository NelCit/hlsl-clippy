// buffer-load-width-vs-cache-line
//
// Detects 2-4 consecutive scalar `Load(...)` calls on the same
// `ByteAddressBuffer` / `Buffer<...>` whose constant offsets fall within a
// 16-byte cache-line window `[base, base+16)`. On every IHV the buffer
// load instruction can fetch a `Load4` (16 B) in one cycle; rewriting the
// scalar loads as one `Load4` saves 3 issue slots and one cache-line round
// trip per load group.
//
// Stage: ControlFlow. Same-block scoping ensures we don't pair loads
// across phi merges where the basic-block-local offsets aren't comparable.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "buffer-load-width-vs-cache-line";
constexpr std::string_view k_category = "control-flow";

[[nodiscard]] std::optional<std::uint32_t> parse_uint_literal(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1U);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == 'u' || s.back() == 'U'))
        s.remove_suffix(1U);
    if (s.empty())
        return std::nullopt;
    if (s.size() > 2U && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        // Hex literal.
        std::uint32_t v = 0U;
        for (std::size_t i = 2U; i < s.size(); ++i) {
            const char c = s[i];
            std::uint32_t d = 0U;
            if (c >= '0' && c <= '9')
                d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                d = static_cast<std::uint32_t>(c - 'a') + 10U;
            else if (c >= 'A' && c <= 'F')
                d = static_cast<std::uint32_t>(c - 'A') + 10U;
            else
                return std::nullopt;
            v = v * 16U + d;
        }
        return v;
    }
    std::uint32_t v = 0U;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return std::nullopt;
        v = v * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return v;
}

struct LoadCall {
    ::TSNode call;
    std::string receiver;  ///< buffer name as it appears in the AST
    std::uint32_t offset;
};

void collect_loads(::TSNode node,
                   std::string_view bytes,
                   std::vector<LoadCall>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        // Match `<recv>.Load` (NOT Load2/3/4 which already widen).
        const auto pos = fn_text.rfind(".Load");
        if (pos != std::string_view::npos && pos + 5U == fn_text.size()) {
            // Pull the receiver text (the part before `.Load`).
            std::string_view receiver = fn_text.substr(0U, pos);
            // Single-arg Load with a constant offset.
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 1U) {
                const auto arg = ::ts_node_named_child(args, 0);
                const auto arg_text = node_text(arg, bytes);
                const auto off = parse_uint_literal(arg_text);
                if (off.has_value()) {
                    out.push_back(LoadCall{
                        .call = node,
                        .receiver = std::string{receiver},
                        .offset = *off,
                    });
                }
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_loads(::ts_node_child(node, i), bytes, out);
    }
}

class BufferLoadWidthVsCacheLine : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        std::vector<LoadCall> loads;
        collect_loads(::ts_tree_root_node(tree.raw_tree()), bytes, loads);
        if (loads.size() < 2U)
            return;
        // Bucket by (block, receiver).
        struct Key {
            std::uint32_t block;
            std::string receiver;
            bool operator==(const Key& other) const noexcept {
                return block == other.block && receiver == other.receiver;
            }
        };
        struct Hash {
            std::size_t operator()(const Key& k) const noexcept {
                std::size_t h = std::hash<std::uint32_t>{}(k.block);
                h ^= std::hash<std::string>{}(k.receiver) + 0x9e3779b9U + (h << 6U) + (h >> 2U);
                return h;
            }
        };
        std::unordered_map<Key, std::vector<LoadCall>, Hash> by_block_recv;
        for (const auto& l : loads) {
            const auto call_span = Span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(l.call),
            };
            const auto block = rules::util::block_for(cfg, call_span);
            if (!block.has_value())
                continue;
            by_block_recv[Key{.block = block->raw(), .receiver = l.receiver}].push_back(l);
        }
        for (auto& [key, group] : by_block_recv) {
            if (group.size() < 2U)
                continue;
            // Sort by offset.
            std::sort(group.begin(), group.end(),
                      [](const LoadCall& a, const LoadCall& b) { return a.offset < b.offset; });
            // Find a window of 2-4 loads inside [base, base+16).
            for (std::size_t i = 0; i < group.size(); ++i) {
                std::size_t j = i;
                while (j + 1U < group.size() && group[j + 1U].offset < group[i].offset + 16U) {
                    ++j;
                }
                const auto count = j - i + 1U;
                if (count >= 2U && count <= 4U && (j != i)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{
                        .source = tree.source_id(),
                        .bytes = tree.byte_range(group[i].call),
                    };
                    diag.message = std::to_string(count) +
                                   " consecutive `" + key.receiver +
                                   ".Load` calls in this basic block fall within a 16-byte "
                                   "window -- coalesce them into a single `Load" +
                                   std::to_string(count) +
                                   "(<base>)` to save issue slots and one cache-line round "
                                   "trip per group";
                    ctx.emit(std::move(diag));
                    i = j;
                }
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_buffer_load_width_vs_cache_line() {
    return std::make_unique<BufferLoadWidthVsCacheLine>();
}

}  // namespace hlsl_clippy::rules
