// Unit tests for `core/src/rules/util/sm6_10.{hpp,cpp}` (ADR 0018 v0.8+
// shared infrastructure).
//
// Coverage:
//   - `target_is_sm610_or_later` accepts `sm_6_10`, `sm_6_10-preview`,
//     `sm_6_11`, and rejects sub-6.10 / empty / garbage profiles.
//   - `is_linalg_matrix_type` matches the SM 6.10 `linalg::Matrix<...>`
//     spelling (with whitespace tolerance) and rejects the SM 6.9 predecessor
//     `vector::CooperativeVector<...>` and unrelated types.
//   - `parse_groupshared_limit_attribute` extracts the byte argument from
//     `[GroupSharedLimit(<bytes>)]` and returns nullopt when absent.
//   - `expected_wave_size_for_target` returns 32 for SM 6.5+ / empty,
//     64 for sub-6.5.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <tree_sitter/api.h>

#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/sm6_10.hpp"

#include "parser_internal.hpp"

namespace {

using shader_clippy::AstTree;
using shader_clippy::ReflectionInfo;
using shader_clippy::SourceManager;

namespace util = shader_clippy::rules::util;

[[nodiscard]] ReflectionInfo make_reflection_with_profile(std::string profile) {
    ReflectionInfo r;
    r.target_profile = std::move(profile);
    return r;
}

/// Parse synthetic HLSL and produce a `ParsedSource` whose tree pointer
/// stays alive for the duration of the test (the caller owns the
/// `ParsedSource` and constructs an `AstTree` value over it).
[[nodiscard]] shader_clippy::parser::ParsedSource parse_source(
    SourceManager& sources, const std::string& hlsl, const std::string& name = "sm6_10_test.hlsl") {
    const auto src = sources.add_buffer(name, hlsl);
    REQUIRE(src.valid());
    auto parsed_opt = shader_clippy::parser::parse(sources, src);
    REQUIRE(parsed_opt.has_value());
    return std::move(parsed_opt.value());
}

/// Walk the tree-sitter root looking for the first `function_definition` /
/// `function_declarator` so the `parse_groupshared_limit_attribute` test
/// can hand it the entry-point node directly.
[[nodiscard]] ::TSNode find_first_function(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return node;
    }
    const char* type = ::ts_node_type(node);
    const std::string_view kind = (type != nullptr) ? std::string_view{type} : std::string_view{};
    if (kind == "function_definition" || kind == "function_declarator") {
        return node;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(node, i);
        const ::TSNode hit = find_first_function(child);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

}  // namespace

TEST_CASE("target_is_sm610_or_later accepts SM 6.10+ and the -preview suffix",
          "[util][sm6_10][target]") {
    CHECK(util::target_is_sm610_or_later("sm_6_10"));
    CHECK(util::target_is_sm610_or_later("sm_6_10-preview"));
    CHECK(util::target_is_sm610_or_later("sm_6_11"));
    // Stage-tagged profile strings round-trip too (proposal 0049 is also
    // valid on `cs_6_10` since it's an entry-point attribute on compute).
    CHECK(util::target_is_sm610_or_later("cs_6_10"));

    CHECK_FALSE(util::target_is_sm610_or_later("sm_6_9"));
    CHECK_FALSE(util::target_is_sm610_or_later("sm_6_6"));
    CHECK_FALSE(util::target_is_sm610_or_later(""));
    CHECK_FALSE(util::target_is_sm610_or_later("ham_sandwich"));

    // Reflection overload mirrors the string overload.
    const auto r10 = make_reflection_with_profile("sm_6_10");
    const auto r10p = make_reflection_with_profile("sm_6_10-preview");
    const auto r9 = make_reflection_with_profile("sm_6_9");
    CHECK(util::target_is_sm610_or_later(r10));
    CHECK(util::target_is_sm610_or_later(r10p));
    CHECK_FALSE(util::target_is_sm610_or_later(r9));
}

TEST_CASE("is_linalg_matrix_type matches linalg::Matrix and tolerates whitespace",
          "[util][sm6_10][linalg]") {
    CHECK(util::is_linalg_matrix_type("linalg::Matrix<float, 4, 4>"));
    CHECK(util::is_linalg_matrix_type("linalg::Matrix < half , 8 , 8 >"));
    CHECK(util::is_linalg_matrix_type("linalg::Matrix<int, 16, 16>"));

    // Non-matches: SM 6.9 predecessor, unrelated types, missing template-arg
    // brackets.
    CHECK_FALSE(util::is_linalg_matrix_type("vector::CooperativeVector<float, 16>"));
    CHECK_FALSE(util::is_linalg_matrix_type("Texture2D<float4>"));
    CHECK_FALSE(util::is_linalg_matrix_type("linalg::MatrixFoo<float, 4, 4>"));
    CHECK_FALSE(util::is_linalg_matrix_type("linalg::Matrix"));  // no `<`.
    CHECK_FALSE(util::is_linalg_matrix_type(""));
    CHECK_FALSE(util::is_linalg_matrix_type("float4x4"));
}

TEST_CASE("parse_groupshared_limit_attribute reads [GroupSharedLimit(<bytes>)] when present",
          "[util][sm6_10][groupshared-limit]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
[GroupSharedLimit(65536)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());
    const ::TSNode entry = find_first_function(root);
    REQUIRE_FALSE(::ts_node_is_null(entry));

    const auto bytes = util::parse_groupshared_limit_attribute(tree, entry);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == 65536U);
}

TEST_CASE("parse_groupshared_limit_attribute returns nullopt when the attribute is absent",
          "[util][sm6_10][groupshared-limit]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
void cs_no_limit(uint3 tid : SV_DispatchThreadID) {
}
)hlsl";
    auto parsed = parse_source(sources, hlsl);
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());
    const ::TSNode entry = find_first_function(root);
    REQUIRE_FALSE(::ts_node_is_null(entry));

    CHECK_FALSE(util::parse_groupshared_limit_attribute(tree, entry).has_value());

    // Null entry-point node: nullopt by definition.
    CHECK_FALSE(util::parse_groupshared_limit_attribute(tree, ::TSNode{}).has_value());
}

TEST_CASE("expected_wave_size_for_target returns 32 for SM 6.5+ and 64 for older",
          "[util][sm6_10][wave-size]") {
    // SM 6.5+: wave32 portable default.
    CHECK(util::expected_wave_size_for_target("sm_6_6") == 32U);
    CHECK(util::expected_wave_size_for_target("sm_6_5") == 32U);
    CHECK(util::expected_wave_size_for_target("sm_6_8") == 32U);
    CHECK(util::expected_wave_size_for_target("sm_6_10") == 32U);
    CHECK(util::expected_wave_size_for_target("sm_6_10-preview") == 32U);
    CHECK(util::expected_wave_size_for_target("cs_6_6") == 32U);

    // Empty / unknown profile: modern default is 32 (RDNA2+ / NVIDIA / Xe2).
    CHECK(util::expected_wave_size_for_target("") == 32U);
    CHECK(util::expected_wave_size_for_target("ham_sandwich") == 32U);

    // Older profiles: wave64 (AMD GCN historical default).
    CHECK(util::expected_wave_size_for_target("sm_6_4") == 64U);
    CHECK(util::expected_wave_size_for_target("sm_6_0") == 64U);
    CHECK(util::expected_wave_size_for_target("vs_6_4") == 64U);
}
