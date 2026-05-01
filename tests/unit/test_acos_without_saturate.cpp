// End-to-end tests for the acos-without-saturate rule.

#include <memory>
#include <string>
#include <string_view>
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
    const auto src = sources.add_buffer("acos.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
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

TEST_CASE("acos-without-saturate fires on acos(dot(...))", "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(dot(normalize(a), normalize(b)));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "acos-without-saturate"));
}

TEST_CASE("acos-without-saturate does not fire on acos(saturate(dot(...)))",
          "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(saturate(dot(normalize(a), normalize(b))));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "acos-without-saturate");
}

TEST_CASE("acos-without-saturate does not fire on acos(clamp(dot(...), -1, 1))",
          "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(clamp(dot(a, b), -1.0, 1.0));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "acos-without-saturate");
}

TEST_CASE("acos-without-saturate fires on asin(dot(...)) too", "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float arc(float3 a, float3 b) {
    return asin(dot(a, b));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "acos-without-saturate"));
}
