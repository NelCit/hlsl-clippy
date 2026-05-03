// End-to-end tests for the inv-sqrt-to-rsqrt rule.
// 1.0 / sqrt(x) -> rsqrt(x), machine-applicable.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

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

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("inv-sqrt-to-rsqrt fires on 1.0 / sqrt(x)", "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0 / sqrt(x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "inv-sqrt-to-rsqrt"));
}

TEST_CASE("inv-sqrt-to-rsqrt fires on 1 / sqrt(x) (integer literal)",
          "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1 / sqrt(x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "inv-sqrt-to-rsqrt"));
}

TEST_CASE("inv-sqrt-to-rsqrt fires on 1.0f / sqrt(x)", "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0f / sqrt(x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "inv-sqrt-to-rsqrt"));
}

TEST_CASE("inv-sqrt-to-rsqrt does not fire on 2.0 / sqrt(x)",
          "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 2.0 / sqrt(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "inv-sqrt-to-rsqrt");
}

TEST_CASE("inv-sqrt-to-rsqrt does not fire on sqrt(x) / 1.0",
          "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return sqrt(x) / 1.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "inv-sqrt-to-rsqrt");
}

TEST_CASE("inv-sqrt-to-rsqrt does not fire on 1.0 / x", "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0 / x; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "inv-sqrt-to-rsqrt");
}

TEST_CASE("inv-sqrt-to-rsqrt does not fire on 1.0 / pow(x, 0.5)",
          "[rules][inv-sqrt-to-rsqrt]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0 / pow(x, 0.5); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "inv-sqrt-to-rsqrt");
}

TEST_CASE("inv-sqrt-to-rsqrt fix is machine-applicable", "[rules][inv-sqrt-to-rsqrt][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0 / sqrt(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "inv-sqrt-to-rsqrt");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "rsqrt(x)");
}
