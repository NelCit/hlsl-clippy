// rwbuffer-store-without-globallycoherent
//
// Detects an `RWBuffer<T>` / `RWStructuredBuffer<T>` / `RWByteAddressBuffer`
// declaration that is written to AND read from inside the same compute /
// pipeline source without a `globallycoherent` qualifier on the declaration.
// Cross-wave readers see arbitrary stale data on RDNA / Ada / Xe-HPG when
// the per-CU L1 holds the producer's write — a classic "works in dev, fails
// in prod" hazard.
//
// Stage: Ast.
//
// Detection plan: scan the source for `RW{Buffer,StructuredBuffer,
// ByteAddressBuffer,Texture*}` declarations whose qualifier list lacks the
// `globallycoherent` keyword. For each such resource, look up uses of the
// resource's identifier in the source: if any indexed write (`name[i] = ...`
// or `name.Store...(...)`) co-exists with any read of the same resource
// (`= name[i]` or `name.Load(...)`) anywhere in the file, emit on the
// declaration span.
//
// The full Phase 4 design (per the rule's doc page) wants CFG analysis to
// confirm that the producing write and consuming read are reachable in the
// same dispatch on cross-wave paths. The doc page accepts that the rule
// fires on the syntactic shape and trusts the suppression mechanism for
// algorithms that legitimately use a single-wave pattern; this rule
// implements that conservative shape exactly. The shape lines up cleanly
// with Stage::Ast and avoids gating Pack B's PR on the engine's per-block
// cross-wave reachability table (which the engine does not yet record).

#include <array>
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

constexpr std::string_view k_rule_id = "rwbuffer-store-without-globallycoherent";
constexpr std::string_view k_category = "bindings";
constexpr std::string_view k_qualifier = "globallycoherent";

constexpr std::array<std::string_view, 4> k_rw_kinds{
    "RWBuffer",
    "RWStructuredBuffer",
    "RWByteAddressBuffer",
    "RWTexture2D",
};

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

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

/// Find `keyword` as a complete identifier token in `text`. Returns
/// `std::string_view::npos` on miss.
[[nodiscard]] std::size_t find_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0U;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return std::string_view::npos;
        }
        const bool ok_left = (found == 0U) || !is_id_char(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || !is_id_char(text[end]);
        if (ok_left && ok_right) {
            return found;
        }
        pos = found + 1U;
    }
    return std::string_view::npos;
}

/// True when `decl_text` declares an RW resource (one of the kinds listed
/// in `k_rw_kinds`) WITHOUT a `globallycoherent` qualifier preceding the
/// type token.
[[nodiscard]] bool is_uncoherent_rw_decl(std::string_view decl_text,
                                         std::string_view& matched_kind) noexcept {
    for (const auto kind : k_rw_kinds) {
        const auto kind_pos = find_keyword(decl_text, kind);
        if (kind_pos == std::string_view::npos) {
            continue;
        }
        // Check whether `globallycoherent` appears before the RW kind token.
        const auto qual_pos = find_keyword(decl_text.substr(0, kind_pos), k_qualifier);
        if (qual_pos == std::string_view::npos) {
            matched_kind = kind;
            return true;
        }
    }
    return false;
}

/// Extract the identifier name declared by `decl_text`. Naive: walk forward
/// from the matched RW-kind keyword to the closing `>` of the templated
/// type, then read the next identifier token. Returns an empty view on
/// shape mismatch.
[[nodiscard]] std::string_view extract_decl_name(std::string_view decl_text,
                                                 std::string_view kind) noexcept {
    const auto kind_pos = find_keyword(decl_text, kind);
    if (kind_pos == std::string_view::npos) {
        return {};
    }
    std::size_t i = kind_pos + kind.size();
    // Skip a templated parameter list `<...>` if present.
    if (i < decl_text.size() && decl_text[i] == '<') {
        int depth = 0;
        while (i < decl_text.size()) {
            if (decl_text[i] == '<') {
                ++depth;
            } else if (decl_text[i] == '>') {
                --depth;
                if (depth == 0) {
                    ++i;
                    break;
                }
            }
            ++i;
        }
    }
    while (i < decl_text.size() && (decl_text[i] == ' ' || decl_text[i] == '\t')) {
        ++i;
    }
    const std::size_t name_start = i;
    while (i < decl_text.size() && is_id_char(decl_text[i])) {
        ++i;
    }
    if (name_start == i) {
        return {};
    }
    return decl_text.substr(name_start, i - name_start);
}

/// True when `bytes` contains both a write (`name[...] = ...` OR `name.Store`)
/// and a read (`= name[...]` OR `name.Load(`) outside the declaration text.
[[nodiscard]] bool sees_write_and_read(std::string_view bytes,
                                       std::string_view name,
                                       std::uint32_t decl_lo,
                                       std::uint32_t decl_hi) noexcept {
    if (name.empty()) {
        return false;
    }
    bool saw_write = false;
    bool saw_read = false;
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(name, pos);
        if (found == std::string_view::npos) {
            break;
        }
        const std::size_t end = found + name.size();
        const bool ok_left = (found == 0U) || !is_id_char(bytes[found - 1U]);
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        const auto abs = static_cast<std::uint32_t>(found);
        if (!ok_left || !ok_right || (abs >= decl_lo && abs < decl_hi)) {
            pos = found + 1U;
            continue;
        }
        // Determine context.
        std::size_t k = end;
        while (k < bytes.size() && (bytes[k] == ' ' || bytes[k] == '\t')) {
            ++k;
        }
        const char nxt = (k < bytes.size()) ? bytes[k] : '\0';
        if (nxt == '[') {
            // Find the matching `]` then look at what follows.
            int depth = 0;
            std::size_t j = k;
            while (j < bytes.size()) {
                if (bytes[j] == '[') {
                    ++depth;
                } else if (bytes[j] == ']') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                ++j;
            }
            if (j < bytes.size()) {
                std::size_t m = j + 1U;
                while (m < bytes.size() && (bytes[m] == ' ' || bytes[m] == '\t')) {
                    ++m;
                }
                if (m < bytes.size() && bytes[m] == '=' &&
                    (m + 1U >= bytes.size() || bytes[m + 1U] != '=')) {
                    saw_write = true;
                } else {
                    saw_read = true;
                }
            }
        } else if (nxt == '.') {
            // Method call: `.Store...` => write, `.Load(`/`.GetDimensions(` => read.
            const std::size_t mname_start = k + 1U;
            std::size_t mname_end = mname_start;
            while (mname_end < bytes.size() && is_id_char(bytes[mname_end])) {
                ++mname_end;
            }
            const auto mname = bytes.substr(mname_start, mname_end - mname_start);
            if (mname == "Store" || mname == "Store2" || mname == "Store3" || mname == "Store4" ||
                mname == "InterlockedAdd" || mname == "InterlockedExchange" ||
                mname == "InterlockedMin" || mname == "InterlockedMax" ||
                mname == "InterlockedAnd" || mname == "InterlockedOr" ||
                mname == "InterlockedXor") {
                saw_write = true;
            } else if (mname == "Load" || mname == "Load2" || mname == "Load3" ||
                       mname == "Load4" || mname == "GetDimensions") {
                saw_read = true;
            }
        }
        if (saw_write && saw_read) {
            return true;
        }
        pos = end;
    }
    return saw_write && saw_read;
}

void walk(::TSNode node,
          std::string_view bytes,
          const AstTree& tree,
          RuleContext& ctx,
          std::vector<std::string>& seen_names) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto decl_text = node_text(node, bytes);
        std::string_view matched_kind;
        if (is_uncoherent_rw_decl(decl_text, matched_kind)) {
            const auto name = extract_decl_name(decl_text, matched_kind);
            if (!name.empty()) {
                std::string name_str{name};
                bool already = false;
                for (const auto& s : seen_names) {
                    if (s == name_str) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    seen_names.push_back(name_str);
                    const auto decl_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                    const auto decl_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
                    if (sees_write_and_read(bytes, name, decl_lo, decl_hi)) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Error;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message =
                            std::string{"`"} + std::string{matched_kind} + "<...>` `" + name_str +
                            "` is written and read in the same source without `globallycoherent` "
                            "-- cross-wave readers may see stale per-CU L1 data on RDNA / Ada / "
                            "Xe-HPG; add `globallycoherent` or split into two dispatches with a "
                            "UAV barrier between them";
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx, seen_names);
    }
}

class RwbufferStoreWithoutGloballycoherent : public Rule {
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
        std::vector<std::string> seen;
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx, seen);
    }
};

}  // namespace

std::unique_ptr<Rule> make_rwbuffer_store_without_globallycoherent() {
    return std::make_unique<RwbufferStoreWithoutGloballycoherent>();
}

}  // namespace hlsl_clippy::rules
