// Internal-only header: declarative tree-sitter query wrapper.
//
// Rules opt into a TSQuery-driven match loop instead of writing imperative
// AST visitors. The wrapper hides every TSQuery / TSQueryCursor C handle
// behind RAII types, exposes captured nodes by name, and surfaces compile
// errors as `Diagnostic`s with byte offsets into the pattern source.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"

namespace shader_clippy::query {

/// Tiny C++20-compatible stand-in for `std::expected<T, E>`. The error type
/// is fixed to `CompileError` because that's the only place we use it; if we
/// need a generic version later we'll switch to `std::expected` (C++23) when
/// the C++23 baseline lands.
template<typename T, typename E>
class Result {
public:
    Result(T value) : payload_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
    Result(E error) : payload_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(payload_);
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }
    [[nodiscard]] const T& value() const& noexcept {
        return std::get<T>(payload_);
    }
    [[nodiscard]] T&& value() && noexcept {
        return std::get<T>(std::move(payload_));
    }
    [[nodiscard]] const E& error() const& noexcept {
        return std::get<E>(payload_);
    }
    [[nodiscard]] const T& operator*() const& noexcept {
        return value();
    }
    [[nodiscard]] T&& operator*() && noexcept {
        return std::move(*this).value();
    }

private:
    std::variant<T, E> payload_;
};

/// One captured node in a single match. `name` is a borrowed view into the
/// pattern's capture-name buffer owned by the parent `Query`.
struct Capture {
    std::string_view name;
    ::TSNode node{};
};

/// Result of one TSQuery match. The `captures` span is borrowed from the
/// parent `QueryEngine`'s scratch buffer and is only valid for the duration
/// of the callback.
class QueryMatch {
public:
    QueryMatch(std::uint32_t pattern_index, std::vector<Capture> captures) noexcept
        : pattern_index_(pattern_index), captures_(std::move(captures)) {}

    [[nodiscard]] std::uint32_t pattern_index() const noexcept {
        return pattern_index_;
    }
    [[nodiscard]] const std::vector<Capture>& captures() const noexcept {
        return captures_;
    }

    /// Look up a capture by name. Returns a null `TSNode` if absent.
    [[nodiscard]] ::TSNode capture(std::string_view name) const noexcept;

private:
    std::uint32_t pattern_index_ = 0;
    std::vector<Capture> captures_;
};

/// Failure reason for `Query::compile`. Carries the byte offset into the
/// pattern source so callers can build a useful diagnostic.
struct CompileError {
    ::TSQueryError code = ::TSQueryErrorNone;
    std::uint32_t byte_offset = 0;
    std::string detail;  ///< Optional human-readable detail.
};

/// RAII wrapper around `TSQuery*`. Move-only; non-copyable. Compile failures
/// surface as `std::expected` errors rather than throwing.
class Query {
public:
    Query() = default;
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;
    Query(Query&&) noexcept = default;
    Query& operator=(Query&&) noexcept = default;
    ~Query() = default;

    [[nodiscard]] static Result<Query, CompileError> compile(const ::TSLanguage* language,
                                                             std::string_view pattern);

    [[nodiscard]] ::TSQuery* raw() const noexcept {
        return query_.get();
    }

    /// Resolve a numeric capture index to its declared name. Returns an
    /// empty view if `index` is out of range.
    [[nodiscard]] std::string_view capture_name(std::uint32_t index) const noexcept;

private:
    struct Deleter {
        void operator()(::TSQuery* q) const noexcept {
            if (q != nullptr) {
                ::ts_query_delete(q);
            }
        }
    };
    using UniqueQuery = std::unique_ptr<::TSQuery, Deleter>;

    explicit Query(UniqueQuery q) noexcept : query_(std::move(q)) {}

    UniqueQuery query_;
};

/// Stateful match driver. Owns one `TSQueryCursor` and a small scratch
/// buffer reused across matches; instances are not thread-safe but are
/// trivially cheap to construct per lint pass.
class QueryEngine {
public:
    QueryEngine();
    QueryEngine(const QueryEngine&) = delete;
    QueryEngine& operator=(const QueryEngine&) = delete;
    QueryEngine(QueryEngine&&) noexcept = default;
    QueryEngine& operator=(QueryEngine&&) noexcept = default;
    ~QueryEngine() = default;

    /// Iterate every match for `query` rooted at `root_node`. The callback
    /// receives `QueryMatch`es in source order; capture spans inside each
    /// match are valid only for the duration of the callback.
    void run(const Query& query,
             ::TSNode root_node,
             const std::function<void(const QueryMatch&)>& on_match);

private:
    struct CursorDeleter {
        void operator()(::TSQueryCursor* c) const noexcept {
            if (c != nullptr) {
                ::ts_query_cursor_delete(c);
            }
        }
    };
    std::unique_ptr<::TSQueryCursor, CursorDeleter> cursor_;
};

}  // namespace shader_clippy::query
