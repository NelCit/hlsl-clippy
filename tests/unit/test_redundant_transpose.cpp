// End-to-end tests for the redundant-transpose rule.
// transpose(transpose(M)) -> M, machine-applicable.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("redundant-transpose fires on transpose(transpose(M))",
          "[rules][redundant-transpose]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4x4 f(float4x4 M) { return transpose(transpose(M)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-transpose"));
}

TEST_CASE("redundant-transpose fires on a 3x3 float matrix", "[rules][redundant-transpose]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3x3 f(float3x3 M) { return transpose(transpose(M)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-transpose"));
}

TEST_CASE("redundant-transpose does not fire on a single transpose",
          "[rules][redundant-transpose]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4x4 f(float4x4 M) { return transpose(M); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-transpose");
}

TEST_CASE("redundant-transpose does not fire on transpose(mul(A, B))",
          "[rules][redundant-transpose]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4x4 f(float4x4 A, float4x4 B) { return transpose(mul(A, B)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-transpose");
}

TEST_CASE("redundant-transpose fix is machine-applicable and replaces with the matrix",
          "[rules][redundant-transpose][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4x4 f(float4x4 M) { return transpose(transpose(M)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "redundant-transpose") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "M");
}
