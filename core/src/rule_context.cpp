#include <cstdint>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/suppress.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy {

void RuleContext::emit(Diagnostic diag) {
    if (suppressions_ != nullptr &&
        suppressions_->suppresses(
            diag.code, diag.primary_span.bytes.lo, diag.primary_span.bytes.hi)) {
        return;
    }
    diagnostics_.push_back(std::move(diag));
}

std::vector<Diagnostic> RuleContext::take_diagnostics() noexcept {
    return std::move(diagnostics_);
}

// Default `on_node` is a no-op; rules that need to ignore certain node types
// don't have to override.
void Rule::on_node(const AstCursor& /*cursor*/, RuleContext& /*ctx*/) {}

// Default `on_tree` is a no-op; imperative rules ignore it entirely.
void Rule::on_tree(const AstTree& /*tree*/, RuleContext& /*ctx*/) {}

std::string_view AstTree::text(::TSNode node) const noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (hi > source_bytes_.size() || hi < lo) {
        return {};
    }
    return source_bytes_.substr(lo, hi - lo);
}

ByteSpan AstTree::byte_range(::TSNode node) const noexcept {
    if (::ts_node_is_null(node)) {
        return ByteSpan{};
    }
    return ByteSpan{
        .lo = static_cast<std::uint32_t>(::ts_node_start_byte(node)),
        .hi = static_cast<std::uint32_t>(::ts_node_end_byte(node)),
    };
}

}  // namespace hlsl_clippy
