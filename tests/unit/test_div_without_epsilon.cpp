// End-to-end tests for the div-without-epsilon rule.

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
    const auto src = sources.add_buffer("div.hlsl", hlsl);
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

TEST_CASE("div-without-epsilon fires on x / length(...)", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 unit_dir(float3 v) {
    return v / length(v);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "div-without-epsilon"));
}

TEST_CASE("div-without-epsilon fires on x / dot(...)", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ratio(float3 a, float3 b) {
    return 1.0 / dot(a, b);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "div-without-epsilon"));
}

TEST_CASE("div-without-epsilon does not fire on guarded divisor", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ratio(float3 a, float3 b) {
    return 1.0 / max(1e-6, dot(a, b));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "div-without-epsilon");
}

TEST_CASE("div-without-epsilon does not fire on x / 2.0", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float half_of(float x) { return x / 2.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "div-without-epsilon");
}
