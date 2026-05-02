// Register-pressure heuristic implementation -- ADR 0017 sub-phase 7b.2.
//
// For each basic block we sum the bit-widths of its live-in names,
// round each up to a 32-bit VGPR slot (`ceil(bits / 32)`), and emit a
// `PressureEstimate{block, total}`. Bit-widths come from three sources,
// in priority order:
//
//   1. Reflection: cbuffer field types + entry-point parameter types
//      keyed by name. The match is exact-name -- if a variable shadows
//      a cbuffer field, the cbuffer field's bit-width is used. This is
//      conservative for the heuristic's purpose (over-counting tracks
//      worst-case pressure).
//
//   2. Declaration-site lexeme: a single AST walk records the type
//      lexeme adjacent to each declared identifier. We pattern-match a
//      handful of known narrow / wide HLSL type tokens
//      (`min16float`, `half`, `uint16_t`, `double`, ...) and convert
//      to bits. Vector / matrix shapes are honoured (`float3` = 3 *
//      32 bits = 96 bits, vs the scalar `float` = 32 bits).
//
//   3. Default: 32 bits. The vast majority of HLSL temporaries are
//      single-precision floats, so this is the right default.
//
// The whole pass is O(blocks * live-set-size + ast-nodes) per source and
// runs once per lint of a Phase 7-rule-enabled source.

#include "rules/util/register_pressure_ast.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"
#include "parser_internal.hpp"
#include "rules/util/liveness.hpp"

namespace hlsl_clippy::util {

namespace {

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* type = ::ts_node_type(node);
    return type != nullptr ? std::string_view{type} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

/// Convert an HLSL type lexeme (or a cbuffer field's type_name from
/// reflection) into a total bit count. Recognised forms:
///
///   * `bool` / `int8` -> 8 bits (HLSL bool is conventionally 32 bits at
///     the storage level but conceptually 1 bit; we use 32 for VGPR
///     accounting because that matches what the compiler allocates).
///   * `min16*` / `half` / `*16_t` -> 16 bits.
///   * `float` / `int` / `uint` (and `*32_t`) -> 32 bits.
///   * `double` / `int64_t` / `uint64_t` -> 64 bits.
///   * Vector shape suffix `N` (1..4) multiplies bits by N
///     (`float3` -> 96 bits, `min16float4` -> 64 bits, `double2` -> 128).
///   * Matrix shape `RxC` multiplies by R*C (`float4x4` -> 512 bits).
///
/// Returns 0 when the lexeme is empty or unrecognised; callers fall
/// back to the next priority tier.
[[nodiscard]] std::uint32_t type_lexeme_bits(std::string_view type_name) noexcept {
    if (type_name.empty()) {
        return 0U;
    }
    // Strip leading qualifiers: `row_major float4x4` -> `float4x4`.
    static constexpr std::string_view k_prefixes[] = {
        "row_major ", "column_major ", "snorm ", "unorm ", "centroid ", "noperspective ",
        "linear ",    "sample ",       "nointerpolation ", "precise ", "uniform ",
        "static ",    "const ",        "globallycoherent ", "groupshared ",
    };
    while (true) {
        bool stripped = false;
        for (const auto pfx : k_prefixes) {
            if (type_name.starts_with(pfx)) {
                type_name.remove_prefix(pfx.size());
                stripped = true;
                break;
            }
        }
        if (!stripped) {
            break;
        }
    }
    if (type_name.empty()) {
        return 0U;
    }

    // Detect the scalar prefix.
    std::uint32_t scalar_bits = 0U;
    std::string_view rest;
    static constexpr std::pair<std::string_view, std::uint32_t> k_scalar_prefixes[] = {
        {"min16float",  16U}, {"min10float",  16U}, {"min16int",    16U},
        {"min12int",    16U}, {"min16uint",   16U},
        {"float16_t",   16U}, {"int16_t",     16U}, {"uint16_t",    16U},
        {"float32_t",   32U}, {"int32_t",     32U}, {"uint32_t",    32U},
        {"float64_t",   64U}, {"int64_t",     64U}, {"uint64_t",    64U},
        {"double",      64U},
        {"float",       32U}, {"half",        16U},
        {"uint",        32U}, {"int",         32U}, {"bool",        32U},
    };
    for (const auto& [pfx, bits] : k_scalar_prefixes) {
        if (type_name.starts_with(pfx)) {
            scalar_bits = bits;
            rest = type_name.substr(pfx.size());
            break;
        }
    }
    if (scalar_bits == 0U) {
        // `vector<T,N>` and `matrix<T,R,C>` -- recurse on the inner type.
        if (type_name.starts_with("vector<") || type_name.starts_with("matrix<")) {
            const auto open = type_name.find('<');
            const auto close = type_name.rfind('>');
            if (open != std::string_view::npos && close != std::string_view::npos &&
                open + 1U < close) {
                const auto inner = type_name.substr(open + 1U, close - open - 1U);
                // First template arg is the scalar type.
                const auto comma = inner.find(',');
                const auto scalar = comma == std::string_view::npos ? inner : inner.substr(0U, comma);
                std::string scalar_str{scalar};
                // Trim whitespace from the scalar string.
                while (!scalar_str.empty() && (scalar_str.front() == ' ' || scalar_str.front() == '\t')) {
                    scalar_str.erase(0, 1);
                }
                while (!scalar_str.empty() && (scalar_str.back() == ' ' || scalar_str.back() == '\t')) {
                    scalar_str.pop_back();
                }
                std::uint32_t inner_bits = type_lexeme_bits(scalar_str);
                if (inner_bits == 0U) {
                    return 0U;
                }
                // Multiply by the dimension(s) -- best-effort parse of the
                // remaining template args.
                std::uint32_t product = 1U;
                if (comma != std::string_view::npos) {
                    auto rem = inner.substr(comma + 1U);
                    while (!rem.empty()) {
                        const auto next_comma = rem.find(',');
                        const auto tok = next_comma == std::string_view::npos ? rem : rem.substr(0U, next_comma);
                        std::uint32_t v = 0U;
                        for (const char c : tok) {
                            if (c >= '0' && c <= '9') {
                                v = v * 10U + static_cast<std::uint32_t>(c - '0');
                            }
                        }
                        if (v == 0U) {
                            v = 1U;
                        }
                        product *= v;
                        if (next_comma == std::string_view::npos) {
                            break;
                        }
                        rem = rem.substr(next_comma + 1U);
                    }
                }
                return inner_bits * product;
            }
        }
        return 0U;
    }

    // Parse vector / matrix shape suffix: `N`, `NxM`.
    if (rest.empty()) {
        return scalar_bits;
    }
    auto parse_digit = [](char c) -> std::uint32_t {
        return (c >= '0' && c <= '9') ? static_cast<std::uint32_t>(c - '0') : 0U;
    };
    const std::uint32_t a = parse_digit(rest[0]);
    if (a == 0U) {
        return scalar_bits;
    }
    if (rest.size() >= 3U && rest[1] == 'x') {
        const std::uint32_t b = parse_digit(rest[2]);
        if (b != 0U) {
            return scalar_bits * a * b;
        }
    }
    return scalar_bits * a;
}

/// Walk the AST root once and record `name -> declaration-type-lexeme`
/// for every variable declaration in the source. `init_declarator`'s
/// containing declaration node carries the type as its first text-bearing
/// child (the grammar exposes the type via `type_specifier`,
/// `primitive_type`, or `template_type` depending on the variant).
struct DeclTypeMap {
    std::unordered_map<std::string, std::string> type_by_name;
};

void collect_decl_types(::TSNode root, std::string_view bytes, DeclTypeMap& out) {
    if (::ts_node_is_null(root)) {
        return;
    }
    std::vector<::TSNode> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        const auto node = stack.back();
        stack.pop_back();
        const auto kind = node_kind(node);
        const bool decl_like = (kind == "declaration" || kind == "field_declaration" ||
                                kind == "parameter_declaration" || kind == "parameter" ||
                                kind == "variable_declaration");
        if (decl_like) {
            // Find the type lexeme: usually the first named child whose
            // kind looks type-ish.
            std::string_view type_text;
            const std::uint32_t cnt = ::ts_node_child_count(node);
            for (std::uint32_t i = 0; i < cnt; ++i) {
                const auto child = ::ts_node_child(node, i);
                if (::ts_node_is_null(child) || !::ts_node_is_named(child)) {
                    continue;
                }
                const auto ck = node_kind(child);
                if (ck == "primitive_type" || ck == "type_identifier" ||
                    ck == "template_type" || ck == "type_specifier" ||
                    ck == "sized_type_specifier") {
                    type_text = node_text(child, bytes);
                    break;
                }
            }
            if (type_text.empty()) {
                // Fallback: take the first identifier-like text and strip
                // trailing punctuation. Useful when the grammar surfaces
                // the type as bare children.
                for (std::uint32_t i = 0; i < cnt; ++i) {
                    const auto child = ::ts_node_child(node, i);
                    if (::ts_node_is_null(child)) {
                        continue;
                    }
                    if (node_kind(child) == "identifier") {
                        const auto candidate = node_text(child, bytes);
                        if (!candidate.empty()) {
                            type_text = candidate;
                            break;
                        }
                    }
                }
            }

            // Find every identifier name declared by this node.
            // `init_declarator` and bare `declarator` -> name; nested
            // `array_declarator` unwraps.
            std::vector<::TSNode> sub;
            sub.push_back(node);
            while (!sub.empty()) {
                const auto cur = sub.back();
                sub.pop_back();
                const auto ck = node_kind(cur);
                if (ck == "init_declarator" || ck == "field_identifier") {
                    ::TSNode declarator =
                        ::ts_node_child_by_field_name(cur, "declarator", 10U);
                    if (::ts_node_is_null(declarator)) {
                        declarator = ::ts_node_child(cur, 0U);
                    }
                    while (!::ts_node_is_null(declarator) &&
                           node_kind(declarator) == "array_declarator") {
                        const auto inner =
                            ::ts_node_child_by_field_name(declarator, "declarator", 10U);
                        if (::ts_node_is_null(inner)) {
                            break;
                        }
                        declarator = inner;
                    }
                    if (!::ts_node_is_null(declarator)) {
                        const auto name = node_text(declarator, bytes);
                        if (!name.empty() && !type_text.empty()) {
                            out.type_by_name.try_emplace(std::string{name}, std::string{type_text});
                        }
                    }
                } else if (ck == "identifier") {
                    const auto name = node_text(cur, bytes);
                    if (!name.empty() && !type_text.empty()) {
                        out.type_by_name.try_emplace(std::string{name}, std::string{type_text});
                    }
                }
                const std::uint32_t scnt = ::ts_node_child_count(cur);
                for (std::uint32_t i = 0; i < scnt; ++i) {
                    const auto c = ::ts_node_child(cur, i);
                    if (!::ts_node_is_null(c) && ::ts_node_is_named(c)) {
                        sub.push_back(c);
                    }
                }
            }
        }
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const auto child = ::ts_node_child(node, i);
            if (!::ts_node_is_null(child)) {
                stack.push_back(child);
            }
        }
    }
}

/// Reflection lookup -- match an identifier name against cbuffer fields
/// + entry-point parameters and return the bit width of the first hit.
[[nodiscard]] std::uint32_t reflection_bits_for(const ReflectionInfo& reflection,
                                                std::string_view name) noexcept {
    for (const auto& cb : reflection.cbuffers) {
        for (const auto& field : cb.fields) {
            if (field.name == name) {
                const auto via_lexeme = type_lexeme_bits(field.type_name);
                if (via_lexeme != 0U) {
                    return via_lexeme;
                }
                if (field.byte_size != 0U) {
                    return field.byte_size * 8U;
                }
            }
        }
    }
    // Entry-point parameter names are not first-class in `EntryPointInfo`
    // today (the struct exposes name + stage + numthreads only). When
    // they land we'll lookup here; for now reflection only resolves
    // cbuffer fields.
    (void)reflection;
    return 0U;
}

/// Look up the bit width for `name` in priority order: reflection ->
/// declaration-lexeme -> default 32.
[[nodiscard]] std::uint32_t bits_for_name(std::string_view name,
                                          const DeclTypeMap& decl_types,
                                          const ReflectionInfo* reflection) noexcept {
    if (reflection != nullptr) {
        const auto bits = reflection_bits_for(*reflection, name);
        if (bits != 0U) {
            return bits;
        }
    }
    const auto it = decl_types.type_by_name.find(std::string{name});
    if (it != decl_types.type_by_name.end()) {
        const auto bits = type_lexeme_bits(it->second);
        if (bits != 0U) {
            return bits;
        }
    }
    return 32U;
}

/// Round bit count up to a VGPR slot count: `ceil(bits / 32)`.
[[nodiscard]] std::uint32_t bits_to_vgpr_slots(std::uint32_t bits) noexcept {
    if (bits == 0U) {
        return 0U;
    }
    return (bits + 31U) / 32U;
}

[[nodiscard]] const control_flow::CfgStorage* storage_of(const ControlFlowInfo& cfg) noexcept {
    if (cfg.cfg.impl == nullptr) {
        return nullptr;
    }
    const auto& storage = cfg.cfg.impl->data.storage;
    return storage ? storage.get() : nullptr;
}

}  // namespace

std::vector<PressureEstimate> estimate_pressure(const ControlFlowInfo& cfg,
                                                const LivenessInfo& liveness,
                                                const AstTree& tree,
                                                const ReflectionInfo* reflection,
                                                std::uint32_t threshold) {
    (void)threshold;  // forwarded for caller-side filtering convenience

    std::vector<PressureEstimate> result;
    const auto* storage = storage_of(cfg);
    if (storage == nullptr) {
        return result;
    }
    if (tree.raw_tree() == nullptr) {
        return result;
    }
    const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
    if (::ts_node_is_null(root)) {
        return result;
    }

    DeclTypeMap decl_types;
    collect_decl_types(root, tree.source_bytes(), decl_types);

    // Iterate every block id known to the storage. The block list lives
    // in `block_to_function`; raw ids are 1-based.
    result.reserve(storage->block_to_function.size());
    for (std::size_t i = 0; i < storage->block_to_function.size(); ++i) {
        const auto raw = static_cast<std::uint32_t>(i + 1U);
        const BasicBlockId block{raw};
        const auto live = liveness.live_in_at(block);
        std::uint32_t total = 0U;
        for (const auto& name : live) {
            const auto bits = bits_for_name(name, decl_types, reflection);
            total += bits_to_vgpr_slots(bits);
        }
        result.push_back(PressureEstimate{block, total});
    }

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.estimated_vgprs > b.estimated_vgprs;
    });
    return result;
}

}  // namespace hlsl_clippy::util
