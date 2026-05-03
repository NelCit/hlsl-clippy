// End-to-end tests for the branch-on-uniform-missing-attribute rule.

#include <memory>
#include <string>
#include <string_view>
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
    const auto src = sources.add_buffer("buma.hlsl", hlsl);
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

TEST_CASE("branch-on-uniform-missing-attribute fires on uniform-looking branch",
          "[rules][branch-on-uniform-missing-attribute]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Cb {
    float4 settings;
};

float4 main() : SV_Target {
    if (settings.x > 0.5) {
        return float4(1, 0, 0, 1);
    }
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "branch-on-uniform-missing-attribute"));
}

TEST_CASE("branch-on-uniform-missing-attribute does not fire when [branch] is present",
          "[rules][branch-on-uniform-missing-attribute]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Cb {
    float4 settings;
};

float4 main() : SV_Target {
    [branch] if (settings.x > 0.5) {
        return float4(1, 0, 0, 1);
    }
    return float4(0, 0, 0, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "branch-on-uniform-missing-attribute");
}

TEST_CASE("branch-on-uniform-missing-attribute does not fire on SV-divergent branch",
          "[rules][branch-on-uniform-missing-attribute]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs(uint3 tid : SV_DispatchThreadID) {
    if (tid.x > 0) {
        // body
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "branch-on-uniform-missing-attribute");
}
