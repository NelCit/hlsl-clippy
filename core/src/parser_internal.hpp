// Internal-only header: holds the tree-sitter wrappers used by the rule
// engine. Public-header consumers (CLI, LSP) never include this.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <tree_sitter/api.h>

#include "hlsl_clippy/language.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::parser {

// RAII wrappers around the opaque tree-sitter handles.

struct TSParserDeleter {
    void operator()(::TSParser* p) const noexcept {
        if (p != nullptr) {
            ts_parser_delete(p);
        }
    }
};

struct TSTreeDeleter {
    void operator()(::TSTree* t) const noexcept {
        if (t != nullptr) {
            ts_tree_delete(t);
        }
    }
};

using UniqueParser = std::unique_ptr<::TSParser, TSParserDeleter>;
using UniqueTree = std::unique_ptr<::TSTree, TSTreeDeleter>;

/// Parse state for one source. Holds both the parsed tree and a back-pointer
/// to the source bytes. Constructed by `parse()`; consumed by the rule walker.
struct ParsedSource {
    SourceId source;
    std::string_view bytes;
    UniqueTree tree;
    /// Language pointer used for the parse. Held so that declarative rules can
    /// compile a `TSQuery` against the same language without re-discovering it.
    const ::TSLanguage* language = nullptr;
};

/// Parse `source`'s contents. Returns `std::nullopt` if the parser could not
/// be constructed or the parse returned no tree at all. Grammar `ERROR` nodes
/// are *not* fatal: tree-sitter recovers and the rest of the tree is still
/// walkable.
///
/// The single-arg overload re-derives the source language from the file's
/// extension via `detect_language()`. The two-arg overload accepts a
/// pre-resolved `SourceLanguage` and is used by the orchestrator to honour
/// `Config::source_language` overrides on otherwise-ambiguous paths
/// (e.g. an `.hlsl`-extension file that the user has explicitly tagged
/// `[lint] source-language = "slang"`). `Auto` is treated as if the
/// single-arg path had been called.
[[nodiscard]] std::optional<ParsedSource> parse(const SourceManager& sources, SourceId source);
[[nodiscard]] std::optional<ParsedSource> parse(const SourceManager& sources,
                                                SourceId source,
                                                SourceLanguage language);

}  // namespace hlsl_clippy::parser

namespace hlsl_clippy {

/// Concrete tree handle backing the public `AstTree` forward declaration.
/// Holds the parsed `TSTree*`, the language used to parse it, the source
/// bytes, and the source id, so declarative rules can compile a TSQuery
/// against the language and drive a match loop without re-discovering the
/// language.
class AstTree {
public:
    AstTree(::TSTree* tree,
            const ::TSLanguage* language,
            std::string_view source_bytes,
            SourceId source) noexcept
        : tree_(tree), language_(language), source_bytes_(source_bytes), source_(source) {}

    [[nodiscard]] ::TSTree* raw_tree() const noexcept {
        return tree_;
    }
    [[nodiscard]] const ::TSLanguage* language() const noexcept {
        return language_;
    }
    [[nodiscard]] std::string_view source_bytes() const noexcept {
        return source_bytes_;
    }
    [[nodiscard]] SourceId source_id() const noexcept {
        return source_;
    }

    /// Slice the source bytes covered by the given tree-sitter node.
    [[nodiscard]] std::string_view text(::TSNode node) const noexcept;

    /// Half-open byte range covered by the given node.
    [[nodiscard]] ByteSpan byte_range(::TSNode node) const noexcept;

private:
    ::TSTree* tree_;
    const ::TSLanguage* language_;
    std::string_view source_bytes_;
    SourceId source_;
};

/// Concrete cursor backing the public `AstCursor` forward declaration. Holds
/// a tree-sitter node and a view of the underlying source bytes for the run.
class AstCursor {
public:
    AstCursor(::TSNode node, std::string_view source_bytes, SourceId source) noexcept
        : node_(node), source_bytes_(source_bytes), source_(source) {}

    [[nodiscard]] ::TSNode node() const noexcept {
        return node_;
    }
    [[nodiscard]] std::string_view source_bytes() const noexcept {
        return source_bytes_;
    }
    [[nodiscard]] SourceId source_id() const noexcept {
        return source_;
    }

    /// True if the wrapped node is a parser error / has any error descendant.
    [[nodiscard]] bool has_error() const noexcept {
        return ts_node_has_error(node_);
    }

    /// Symbolic name of the node (e.g. `"call_expression"`).
    [[nodiscard]] std::string_view kind() const noexcept {
        const char* type = ts_node_type(node_);
        return type != nullptr ? std::string_view{type} : std::string_view{};
    }

    /// Half-open byte range covered by this node.
    [[nodiscard]] ByteSpan byte_range() const noexcept {
        return ByteSpan{
            static_cast<std::uint32_t>(ts_node_start_byte(node_)),
            static_cast<std::uint32_t>(ts_node_end_byte(node_)),
        };
    }

    /// UTF-8 text covered by this node, viewed into the source buffer.
    [[nodiscard]] std::string_view text() const noexcept {
        const auto range = byte_range();
        if (range.lo > source_bytes_.size() || range.hi > source_bytes_.size() ||
            range.hi < range.lo) {
            return {};
        }
        return source_bytes_.substr(range.lo, range.hi - range.lo);
    }

    /// Text covered by a child node accessed via field name (e.g. `"function"`).
    /// Returns an empty view if the field is absent.
    [[nodiscard]] ::TSNode child_by_field(std::string_view field_name) const noexcept {
        return ts_node_child_by_field_name(
            node_, field_name.data(), static_cast<std::uint32_t>(field_name.size()));
    }

private:
    ::TSNode node_;
    std::string_view source_bytes_;
    SourceId source_;
};

}  // namespace hlsl_clippy
