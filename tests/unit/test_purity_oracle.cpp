// Unit tests for the v1.2 purity oracle (`core/src/rules/util/purity_oracle.*`,
// ADR 0019 §"v1.x patch trajectory"). Coverage:
//
//   * `x` (bare identifier) -> SideEffectFree.
//   * `a.b.c` (chained field access) -> SideEffectFree.
//   * `arr[i]` (subscript expression) -> SideEffectFree.
//   * `1.0 + 2.0 * x` (binary expressions over leaves) -> SideEffectFree.
//   * `f(x)` (unknown call target) -> SideEffectful.
//   * `saturate(x * 2.0)` (allowlisted intrinsic, pure args) -> SideEffectFree.
//   * `g_buffer[i] = 0` (assignment expression) -> SideEffectful.
//   * `i++` (update expression) -> SideEffectful.
//   * `someFunc()` (no allowlist match, no args) -> SideEffectful.
//
// The tests synthesise a small HLSL function around each expression so the
// tree-sitter parser produces a real AST node, then walk the function body
// to find the expression of interest by node kind + textual match.

#include <cstdint>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <tree_sitter/api.h>

#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/purity_oracle.hpp"

#include "parser_internal.hpp"

namespace {

using shader_clippy::AstTree;
using shader_clippy::SourceManager;
using shader_clippy::rules::util::Purity;
namespace util = shader_clippy::rules::util;

/// Parse `hlsl` and return the resulting `ParsedSource`. The caller pins
/// the returned object so the underlying `TSTree*` outlives every node
/// view we hand out.
[[nodiscard]] shader_clippy::parser::ParsedSource parse_source(SourceManager& sources,
                                                             const std::string& hlsl,
                                                             const std::string& name) {
    const auto src = sources.add_buffer(name, hlsl);
    REQUIRE(src.valid());
    auto parsed_opt = shader_clippy::parser::parse(sources, src);
    REQUIRE(parsed_opt.has_value());
    return std::move(parsed_opt.value());
}

/// Walk every named descendant of `root` and return the first one whose kind
/// matches `kind` and whose source text equals `needle`. Returns the null
/// `TSNode` when no match is found; callers REQUIRE `!is_null` to fail loudly
/// rather than silently dispatching the oracle on a bogus span.
///
/// Implementation: depth-first recursion over named children. Tree-sitter's
/// node-kind names are stable across the grammars we vendor (`identifier`,
/// `field_expression`, ...); we match on textual equality of the source
/// slice to disambiguate when the same kind appears multiple times.
[[nodiscard]] ::TSNode find_node_by_kind_and_text(::TSNode root,
                                                  std::string_view bytes,
                                                  std::string_view kind,
                                                  std::string_view needle) {
    if (::ts_node_is_null(root)) {
        return root;
    }
    if (util::node_kind(root) == kind) {
        const auto text = util::node_text(root, bytes);
        if (text == needle) {
            return root;
        }
    }
    const std::uint32_t count = ::ts_node_named_child_count(root);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_named_child(root, i);
        const ::TSNode hit = find_node_by_kind_and_text(child, bytes, kind, needle);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

/// Like `find_node_by_kind_and_text` but matches only on `kind` -- returns
/// the first node of the given kind in DFS order.
[[nodiscard]] ::TSNode find_node_by_kind(::TSNode root, std::string_view kind) {
    if (::ts_node_is_null(root)) {
        return root;
    }
    if (util::node_kind(root) == kind) {
        return root;
    }
    const std::uint32_t count = ::ts_node_named_child_count(root);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_named_child(root, i);
        const ::TSNode hit = find_node_by_kind(child, kind);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

}  // namespace

TEST_CASE("classify_expression on a bare identifier returns SideEffectFree",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float take(float x) {
    return x;
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_ident.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    // The `return x;` statement contains an `identifier` node whose text is
    // `x`. Locate it and classify directly.
    const ::TSNode ident = find_node_by_kind_and_text(root, parsed.bytes, "identifier", "x");
    REQUIRE_FALSE(::ts_node_is_null(ident));
    CHECK(util::classify_expression(tree, ident) == Purity::SideEffectFree);
}

TEST_CASE("classify_expression on chained field access (`a.b.c`) is SideEffectFree",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Inner { float c; };
struct Outer { Inner b; };
float chained(Outer a) {
    return a.b.c;
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_field.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode field =
        find_node_by_kind_and_text(root, parsed.bytes, "field_expression", "a.b.c");
    REQUIRE_FALSE(::ts_node_is_null(field));
    CHECK(util::classify_expression(tree, field) == Purity::SideEffectFree);
}

TEST_CASE("classify_expression on a subscript (`arr[i]`) is SideEffectFree",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float take(uint i) {
    float arr[4] = { 0.0, 1.0, 2.0, 3.0 };
    return arr[i];
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_subscript.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode sub =
        find_node_by_kind_and_text(root, parsed.bytes, "subscript_expression", "arr[i]");
    REQUIRE_FALSE(::ts_node_is_null(sub));
    CHECK(util::classify_expression(tree, sub) == Purity::SideEffectFree);
}

TEST_CASE("classify_expression on `1.0 + 2.0 * x` is SideEffectFree",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float take(float x) {
    return 1.0 + 2.0 * x;
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_binary.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    // The outermost `binary_expression` covers `1.0 + 2.0 * x`. Use a
    // textual probe to find the `+` form (the inner `2.0 * x` is also a
    // binary_expression, so search by exact text).
    const ::TSNode bin =
        find_node_by_kind_and_text(root, parsed.bytes, "binary_expression", "1.0 + 2.0 * x");
    REQUIRE_FALSE(::ts_node_is_null(bin));
    CHECK(util::classify_expression(tree, bin) == Purity::SideEffectFree);
}

TEST_CASE("classify_expression on `f(x)` (unknown call target) is SideEffectful",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float v);  // forward decl, unknown body to the oracle
float take(float x) {
    return f(x);
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_unknown_call.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode call =
        find_node_by_kind_and_text(root, parsed.bytes, "call_expression", "f(x)");
    REQUIRE_FALSE(::ts_node_is_null(call));
    CHECK(util::classify_expression(tree, call) == Purity::SideEffectful);
}

TEST_CASE("classify_expression on `saturate(x * 2.0)` is SideEffectFree",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float take(float x) {
    return saturate(x * 2.0);
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_allowlist_call.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode call =
        find_node_by_kind_and_text(root, parsed.bytes, "call_expression", "saturate(x * 2.0)");
    REQUIRE_FALSE(::ts_node_is_null(call));
    CHECK(util::classify_expression(tree, call) == Purity::SideEffectFree);
}

TEST_CASE("classify_expression on `g_buffer[i] = 0` is SideEffectful",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWBuffer<uint> g_buffer;
void take(uint i) {
    g_buffer[i] = 0;
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_assign.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode assign = find_node_by_kind(root, "assignment_expression");
    REQUIRE_FALSE(::ts_node_is_null(assign));
    CHECK(util::classify_expression(tree, assign) == Purity::SideEffectful);
}

TEST_CASE("classify_expression on `i++` is SideEffectful", "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void take(inout uint i) {
    i++;
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_update.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode upd = find_node_by_kind(root, "update_expression");
    REQUIRE_FALSE(::ts_node_is_null(upd));
    CHECK(util::classify_expression(tree, upd) == Purity::SideEffectful);
}

TEST_CASE("classify_expression on `someFunc()` (no allowlist match) is SideEffectful",
          "[util][purity-oracle]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float someFunc();
float take() {
    return someFunc();
}
)hlsl";
    auto parsed = parse_source(sources, hlsl, "purity_no_arg_call.hlsl");
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());

    const ::TSNode call =
        find_node_by_kind_and_text(root, parsed.bytes, "call_expression", "someFunc()");
    REQUIRE_FALSE(::ts_node_is_null(call));
    CHECK(util::classify_expression(tree, call) == Purity::SideEffectful);
}
