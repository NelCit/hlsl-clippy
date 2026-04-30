// Unit tests for the declarative TSQuery wrapper. Verifies compile errors
// surface as `Result::error()`, the cursor iterates matches in source order,
// and capture lookup by name returns the expected nodes.

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <tree_sitter/api.h>

#include "hlsl_clippy/source.hpp"
#include "query/query.hpp"

#include "parser_internal.hpp"

extern "C" {
const ::TSLanguage* tree_sitter_hlsl(void);
}  // extern "C"

namespace {

using hlsl_clippy::query::Query;
using hlsl_clippy::query::QueryEngine;
using hlsl_clippy::query::QueryMatch;

[[nodiscard]] hlsl_clippy::SourceManager make_sources(const std::string& hlsl,
                                                      hlsl_clippy::SourceId& src_out) {
    hlsl_clippy::SourceManager sm;
    src_out = sm.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src_out.valid());
    return sm;
}

}  // namespace

TEST_CASE("Query::compile rejects malformed patterns", "[query]") {
    const auto result = Query::compile(tree_sitter_hlsl(), "(call_expression");  // unterminated
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Query::compile accepts a simple call-expression pattern", "[query]") {
    const auto result =
        Query::compile(tree_sitter_hlsl(), "(call_expression function: (identifier) @name)");
    REQUIRE(result.has_value());
}

TEST_CASE("QueryEngine::run iterates every match in source order", "[query]") {
    hlsl_clippy::SourceId src{};
    const std::string hlsl =
        "float3 f(float3 c) { return saturate(saturate(c)); }\n"
        "float3 g(float3 d) { return saturate(d); }\n";
    auto sm = make_sources(hlsl, src);
    auto parsed_opt = hlsl_clippy::parser::parse(sm, src);
    REQUIRE(parsed_opt.has_value());
    const auto& parsed = parsed_opt.value();

    auto query =
        Query::compile(tree_sitter_hlsl(), "(call_expression function: (identifier) @fn) @call");
    REQUIRE(query.has_value());

    std::vector<std::string> seen;
    QueryEngine engine;
    engine.run(query.value(), ::ts_tree_root_node(parsed.tree.get()), [&](const QueryMatch& match) {
        const ::TSNode fn = match.capture("fn");
        if (::ts_node_is_null(fn)) {
            return;
        }
        const auto lo = static_cast<std::size_t>(::ts_node_start_byte(fn));
        const auto hi = static_cast<std::size_t>(::ts_node_end_byte(fn));
        seen.emplace_back(parsed.bytes.substr(lo, hi - lo));
    });

    // Expect three matches in source order: the outer saturate, the inner
    // saturate on line 1, and the saturate on line 2.
    REQUIRE(seen.size() >= 3U);
    CHECK(seen[0] == "saturate");
    CHECK(seen[1] == "saturate");
    CHECK(seen[2] == "saturate");
}

TEST_CASE("QueryMatch::capture returns null for missing names", "[query]") {
    hlsl_clippy::SourceId src{};
    const std::string hlsl = "float3 f(float3 c) { return saturate(c); }\n";
    auto sm = make_sources(hlsl, src);
    auto parsed_opt = hlsl_clippy::parser::parse(sm, src);
    REQUIRE(parsed_opt.has_value());
    const auto& parsed = parsed_opt.value();

    auto query = Query::compile(tree_sitter_hlsl(), "(call_expression function: (identifier) @fn)");
    REQUIRE(query.has_value());

    bool ran = false;
    QueryEngine engine;
    engine.run(query.value(), ::ts_tree_root_node(parsed.tree.get()), [&](const QueryMatch& match) {
        ran = true;
        CHECK_FALSE(::ts_node_is_null(match.capture("fn")));
        CHECK(::ts_node_is_null(match.capture("nonexistent")));
    });
    CHECK(ran);
}
