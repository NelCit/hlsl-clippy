// groupshared-16bit-unpacked
//
// Detects `groupshared min16float[N]` / `groupshared uint16_t[N]` /
// `groupshared half[N]` arrays whose every read site immediately widens to a
// 32-bit local or arithmetic operand. RDNA 2/3 packs 16-bit LDS lanes
// 2-per-bank only when consumed via packed-math intrinsics (`fma_pk_f16` and
// friends); widening at the load site collapses the LDS-bandwidth saving
// because the compiler has to materialise a 32-bit load anyway.
//
// Detection (purely AST, conservative):
//   1. Find every `groupshared` declaration whose top-level type token is one
//      of the targeted 16-bit primitives (`min16float`, `uint16_t`,
//      `int16_t`, `float16_t`, `half`, `min16uint`, `min16int`).
//   2. For each such declaration, record the variable name.
//   3. Walk the rest of the tree looking for `subscript_expression` reads
//      where the receiver identifier matches a recorded 16-bit-LDS name.
//   4. If the parent expression of the read is one of:
//        - an `assignment_expression` whose left-hand side is a 32-bit local
//          (cast / declarator with type token in {`float`, `uint`, `int`}),
//        - a `binary_expression` where the sibling operand is a 32-bit
//          numeric literal (no `h` / `H` half-suffix) or a 32-bit-typed
//          identifier;
//      treat the read as widening. If every read site we observe widens, fire.
//
// The fix is suggestion-only: the rewrite involves replacing scalar reads
// with `Load2` / `f16tof32` packed-math idioms whose form depends on the
// surrounding kernel.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-16bit-unpacked";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos)
            return false;
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right)
            return true;
        pos = found + 1U;
    }
    return false;
}

/// True iff `text` contains one of the 16-bit primitive type tokens. The
/// tokens are matched as full identifiers so that `half` does not catch
/// `half4` (which is still 16-bit-per-lane but is only worth flagging when
/// consumed as a vector -- left as future work).
[[nodiscard]] bool has_16bit_type(std::string_view text) noexcept {
    return has_keyword(text, "min16float") || has_keyword(text, "uint16_t") ||
           has_keyword(text, "int16_t") || has_keyword(text, "float16_t") ||
           has_keyword(text, "min16uint") || has_keyword(text, "min16int") ||
           has_keyword(text, "half");
}

/// Extract the variable name from a declaration node by finding the
/// `declarator` field. Returns empty when the declaration shape is unfamiliar.
[[nodiscard]] std::string_view declarator_name(::TSNode decl, std::string_view bytes) noexcept {
    const ::TSNode declarator = ::ts_node_child_by_field_name(decl, "declarator", 10);
    if (::ts_node_is_null(declarator))
        return {};
    ::TSNode name_node = declarator;
    if (node_kind(declarator) == "init_declarator" || node_kind(declarator) == "array_declarator") {
        const ::TSNode inner = ::ts_node_child_by_field_name(declarator, "declarator", 10);
        if (!::ts_node_is_null(inner))
            name_node = inner;
    }
    // Strip another array_declarator wrapper if present.
    if (node_kind(name_node) == "array_declarator") {
        const ::TSNode inner = ::ts_node_child_by_field_name(name_node, "declarator", 10);
        if (!::ts_node_is_null(inner))
            name_node = inner;
    }
    return node_text(name_node, bytes);
}

/// True if `text` parses as a numeric literal with the half-precision suffix
/// (`h`/`H`). Half-suffixed literals do NOT widen the read site.
[[nodiscard]] bool literal_is_half(std::string_view text) noexcept {
    if (text.empty())
        return false;
    const char c = text.back();
    return c == 'h' || c == 'H';
}

/// True if `text` looks like a 32-bit-typed local declarator, by checking the
/// presence of a 32-bit type token (`float` / `uint` / `int`) without a 16-bit
/// qualifier.
[[nodiscard]] bool is_32bit_typed_text(std::string_view text) noexcept {
    if (has_16bit_type(text))
        return false;
    return has_keyword(text, "float") || has_keyword(text, "uint") || has_keyword(text, "int") ||
           has_keyword(text, "double");
}

/// Walk down the tree and collect every groupshared declaration whose type
/// token is 16-bit. We also record whether the declaration is array-shaped
/// (the rule only targets array declarations per the spec).
void collect_decls(::TSNode node,
                   std::string_view bytes,
                   std::unordered_map<std::string, ::TSNode>& out_decls) {
    if (::ts_node_is_null(node))
        return;
    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        if (has_keyword(text, "groupshared") && has_16bit_type(text) &&
            text.find('[') != std::string_view::npos) {
            const auto name = declarator_name(node, bytes);
            if (!name.empty())
                out_decls.emplace(std::string{name}, node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_decls(::ts_node_child(node, i), bytes, out_decls);
    }
}

/// Outcome for one observed read of a 16-bit-LDS array element.
enum class ReadKind : std::uint8_t {
    Widening,    ///< The read is paired with a 32-bit operand or assigned to a 32-bit local.
    Preserving,  ///< The read stays in 16-bit (sibling is half-literal or 16-bit-typed).
    Unknown,     ///< Cannot tell -- treated as preserving to avoid false positives.
};

[[nodiscard]] ReadKind classify_sibling_text(std::string_view text) noexcept {
    if (text.empty())
        return ReadKind::Unknown;
    if (literal_is_half(text))
        return ReadKind::Preserving;
    // Pure decimal / hex literal with no half suffix is a 32-bit operand.
    bool all_numeric = true;
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || c == '.' || c == 'x' || c == 'X' || c == 'u' || c == 'U' ||
              c == 'l' || c == 'L' || c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == '+' ||
              c == '-')) {
            all_numeric = false;
            break;
        }
    }
    if (all_numeric)
        return ReadKind::Widening;
    return ReadKind::Unknown;
}

/// Classify a single subscript_expression read into widening/preserving/unknown.
[[nodiscard]] ReadKind classify_read(::TSNode subscript,
                                     ::TSNode parent,
                                     std::string_view bytes) noexcept {
    if (::ts_node_is_null(parent))
        return ReadKind::Unknown;
    const auto pkind = node_kind(parent);

    if (pkind == "binary_expression") {
        const ::TSNode left = ::ts_node_child_by_field_name(parent, "left", 4);
        const ::TSNode right = ::ts_node_child_by_field_name(parent, "right", 5);
        ::TSNode sibling{};
        if (::ts_node_eq(left, subscript))
            sibling = right;
        else if (::ts_node_eq(right, subscript))
            sibling = left;
        if (::ts_node_is_null(sibling))
            return ReadKind::Unknown;
        const auto sibling_text = node_text(sibling, bytes);
        if (node_kind(sibling) == "number_literal")
            return classify_sibling_text(sibling_text);
        // Identifier / cast / call: be conservative.
        if (node_kind(sibling) == "cast_expression") {
            const ::TSNode tt = ::ts_node_child_by_field_name(sibling, "type", 4);
            const auto type_text = node_text(tt, bytes);
            if (is_32bit_typed_text(type_text))
                return ReadKind::Widening;
        }
        return ReadKind::Unknown;
    }

    if (pkind == "init_declarator" || pkind == "assignment_expression") {
        // `T x = gs[i]` where T is 32-bit -> widening.
        // The init_declarator's parent is a declaration that carries the type.
        ::TSNode grand = ::ts_node_parent(parent);
        if (!::ts_node_is_null(grand)) {
            const ::TSNode type_node = ::ts_node_child_by_field_name(grand, "type", 4);
            if (!::ts_node_is_null(type_node)) {
                const auto type_text = node_text(type_node, bytes);
                if (is_32bit_typed_text(type_text))
                    return ReadKind::Widening;
                if (has_16bit_type(type_text))
                    return ReadKind::Preserving;
            }
        }
        return ReadKind::Unknown;
    }

    if (pkind == "cast_expression") {
        const ::TSNode tt = ::ts_node_child_by_field_name(parent, "type", 4);
        const auto type_text = node_text(tt, bytes);
        if (is_32bit_typed_text(type_text))
            return ReadKind::Widening;
        if (has_16bit_type(type_text))
            return ReadKind::Preserving;
        return ReadKind::Unknown;
    }

    return ReadKind::Unknown;
}

/// For each tracked declaration name, scan the tree for subscript_expression
/// reads and classify each. If at least one read exists and ALL classified
/// reads are widening (Unknown reads are conservatively excluded -- they
/// neither prove nor disprove the pattern), the declaration fires.
struct ReadStats {
    std::uint32_t widening = 0U;
    std::uint32_t preserving = 0U;
    std::uint32_t unknown = 0U;
};

void scan_reads(::TSNode node,
                std::string_view bytes,
                const std::unordered_map<std::string, ::TSNode>& decls,
                std::unordered_map<std::string, ReadStats>& stats) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "subscript_expression") {
        const ::TSNode receiver = ::ts_node_child_by_field_name(node, "argument", 8);
        if (!::ts_node_is_null(receiver) && node_kind(receiver) == "identifier") {
            const auto name = node_text(receiver, bytes);
            const auto it = decls.find(std::string{name});
            if (it != decls.end()) {
                const ::TSNode parent = ::ts_node_parent(node);
                const auto kind = classify_read(node, parent, bytes);
                auto& s = stats[std::string{name}];
                switch (kind) {
                    case ReadKind::Widening:
                        ++s.widening;
                        break;
                    case ReadKind::Preserving:
                        ++s.preserving;
                        break;
                    case ReadKind::Unknown:
                        ++s.unknown;
                        break;
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_reads(::ts_node_child(node, i), bytes, decls, stats);
    }
}

class Groupshared16bitUnpacked : public Rule {
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
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());

        std::unordered_map<std::string, ::TSNode> decls;
        collect_decls(root, bytes, decls);
        if (decls.empty())
            return;

        std::unordered_map<std::string, ReadStats> stats;
        scan_reads(root, bytes, decls, stats);

        for (const auto& [name, decl_node] : decls) {
            const auto it = stats.find(name);
            if (it == stats.end())
                continue;
            const auto& s = it->second;
            // Fire only when at least one widening read exists AND no
            // preserving reads were observed -- if there's any preserving
            // read the array is at least partly used the right way.
            if (s.widening == 0U)
                continue;
            if (s.preserving > 0U)
                continue;

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = tree.byte_range(decl_node)};
            diag.message = std::string{"`groupshared` 16-bit array `"} + name +
                           "` is read into 32-bit operands at every observed site -- "
                           "RDNA 2/3 packs 16-bit LDS lanes 2-per-bank only when "
                           "consumed via packed-math intrinsics; widening at the "
                           "load site collapses the saving";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_16bit_unpacked() {
    return std::make_unique<Groupshared16bitUnpacked>();
}

}  // namespace hlsl_clippy::rules
