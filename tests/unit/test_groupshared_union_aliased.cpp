// End-to-end tests for the groupshared-union-aliased rule.
// Stage::Ast -- detects `groupshared` declarations whose underlying type
// uses `union` keyword or struct-with-bit-fields to alias two views over
// the same offset.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_groupshared_union_aliased();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_groupshared_union_aliased());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rule();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("groupshared-union-aliased fires on a groupshared union",
          "[rules][groupshared-union-aliased]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared union {
    float as_float;
    uint  as_uint;
} gs_view;
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-union-aliased"));
}

TEST_CASE("groupshared-union-aliased fires on groupshared bit-field struct",
          "[rules][groupshared-union-aliased]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct PackedFlags {
    uint a : 8;
    uint b : 8;
    uint c : 16;
};

groupshared PackedFlags gs_flags[64];
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "groupshared-union-aliased"));
}

TEST_CASE("groupshared-union-aliased does not fire on plain groupshared array",
          "[rules][groupshared-union-aliased]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float gs_data[256];
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-union-aliased");
}

TEST_CASE("groupshared-union-aliased does not fire on a non-groupshared union",
          "[rules][groupshared-union-aliased]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Helper {
    float a;
    float b;
};

static Helper g_helper;
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-union-aliased");
}

TEST_CASE("groupshared-union-aliased does not fire on a struct with semantic annotations",
          "[rules][groupshared-union-aliased]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct VsOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

groupshared VsOut gs_vs[16];
)hlsl";
    // `: SV_Position` and `: TEXCOORD0` are NOT bit-fields (no integer
    // literal after the colon). Should stay clean.
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "groupshared-union-aliased");
}
