// Tests for dot4add-opportunity (Phase 8 v0.8 stub-burndown; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_dot4add_opportunity();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("d4o.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_dot4add_opportunity());
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

TEST_CASE("dot4add-opportunity [fix] stub-burndown -- fires on a.x*b.x + ... + a.w*b.w",
          "[rules][dot4add-opportunity][fix]") {
    const std::string hlsl = R"hlsl(
float dot4(float4 a, float4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "dot4add-opportunity"));
}

TEST_CASE("dot4add-opportunity silent on partial 3-component dot",
          "[rules][dot4add-opportunity]") {
    const std::string hlsl = R"hlsl(
float dot3(float4 a, float4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "dot4add-opportunity"));
}
