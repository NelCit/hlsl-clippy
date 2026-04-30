// Declarative TSQuery wrapper implementation.

#include "query/query.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"

namespace hlsl_clippy::query {

::TSNode QueryMatch::capture(std::string_view name) const noexcept {
    for (const auto& c : captures_) {
        if (c.name == name) {
            return c.node;
        }
    }
    return ::TSNode{};
}

Result<Query, CompileError> Query::compile(const ::TSLanguage* language, std::string_view pattern) {
    if (language == nullptr) {
        CompileError err;
        err.code = ::TSQueryErrorLanguage;
        err.detail = "null TSLanguage passed to Query::compile";
        return err;
    }

    std::uint32_t error_offset = 0;
    ::TSQueryError error_code = ::TSQueryErrorNone;
    ::TSQuery* raw = ::ts_query_new(language,
                                    pattern.data(),
                                    static_cast<std::uint32_t>(pattern.size()),
                                    &error_offset,
                                    &error_code);
    if (raw == nullptr || error_code != ::TSQueryErrorNone) {
        if (raw != nullptr) {
            ::ts_query_delete(raw);
        }
        CompileError err;
        err.code = error_code;
        err.byte_offset = error_offset;
        return err;
    }
    return Query{UniqueQuery{raw}};
}

std::string_view Query::capture_name(std::uint32_t index) const noexcept {
    if (query_ == nullptr) {
        return {};
    }
    std::uint32_t length = 0;
    const char* name = ::ts_query_capture_name_for_id(query_.get(), index, &length);
    if (name == nullptr) {
        return {};
    }
    return std::string_view{name, length};
}

QueryEngine::QueryEngine() : cursor_(::ts_query_cursor_new()) {}

void QueryEngine::run(const Query& query,
                      ::TSNode root_node,
                      const std::function<void(const QueryMatch&)>& on_match) {
    if (cursor_ == nullptr || query.raw() == nullptr) {
        return;
    }
    ::ts_query_cursor_exec(cursor_.get(), query.raw(), root_node);

    ::TSQueryMatch raw_match{};
    while (::ts_query_cursor_next_match(cursor_.get(), &raw_match)) {
        std::vector<Capture> captures;
        captures.reserve(raw_match.capture_count);
        for (std::uint16_t i = 0; i < raw_match.capture_count; ++i) {
            const ::TSQueryCapture& rc = raw_match.captures[i];
            Capture c;
            c.name = query.capture_name(rc.index);
            c.node = rc.node;
            captures.push_back(c);
        }
        const QueryMatch m{static_cast<std::uint32_t>(raw_match.pattern_index),
                           std::move(captures)};
        on_match(m);
    }
}

}  // namespace hlsl_clippy::query
