#include "rules/util/ast_helpers.hpp"

#include <cstdint>
#include <string_view>

#include <tree_sitter/api.h>

namespace hlsl_clippy::rules::util {

std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* type = ::ts_node_type(node);
    return type != nullptr ? std::string_view{type} : std::string_view{};
}

std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
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

}  // namespace hlsl_clippy::rules::util
