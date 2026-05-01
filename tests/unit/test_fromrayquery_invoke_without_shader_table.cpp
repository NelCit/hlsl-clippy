// Tests for the fromrayquery-invoke-without-shader-table SER rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_fromrayquery_invoke_without_shader_table();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_fromrayquery_invoke_without_shader_table());
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("fromrayquery-invoke-without-shader-table fires when SetShaderTableIndex missing",
          "[rules][fromrayquery-invoke-without-shader-table]") {
    const std::string hlsl = R"hlsl(
void f() {
    dx::HitObject hit = HitObject::FromRayQuery(q);
    hit.Invoke(payload);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "fromrayquery-invoke-without-shader-table"));
}

TEST_CASE("fromrayquery-invoke-without-shader-table is silent when SetShaderTableIndex present",
          "[rules][fromrayquery-invoke-without-shader-table]") {
    const std::string hlsl = R"hlsl(
void f() {
    dx::HitObject hit = HitObject::FromRayQuery(q);
    hit.SetShaderTableIndex(0);
    hit.Invoke(payload);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "fromrayquery-invoke-without-shader-table");
    }
}
