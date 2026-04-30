#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

extern "C" {
const ::TSLanguage* tree_sitter_hlsl(void);
}  // extern "C"

namespace hlsl_clippy::parser {

std::optional<ParsedSource> parse(const SourceManager& sources, SourceId source) {
    const SourceFile* file = sources.get(source);
    if (file == nullptr) {
        return std::nullopt;
    }

    const UniqueParser parser{ts_parser_new()};
    if (!parser) {
        return std::nullopt;
    }

    if (!ts_parser_set_language(parser.get(), tree_sitter_hlsl())) {
        return std::nullopt;
    }

    const std::string_view bytes = file->contents();
    UniqueTree tree{ts_parser_parse_string(
        parser.get(), nullptr, bytes.data(), static_cast<std::uint32_t>(bytes.size()))};

    if (!tree) {
        return std::nullopt;
    }

    return ParsedSource{.source = source, .bytes = bytes, .tree = std::move(tree)};
}

}  // namespace hlsl_clippy::parser
