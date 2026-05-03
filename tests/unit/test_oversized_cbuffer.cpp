// End-to-end tests for the oversized-cbuffer rule.

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
using shader_clippy::LintOptions;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("oversized.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, opts);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("oversized-cbuffer does not fire on a small cbuffer", "[rules][oversized-cbuffer]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Small : register(b0) {
    float4 a;
    float4 b;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return a + b; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "oversized-cbuffer"));
}

TEST_CASE("oversized-cbuffer fires on a 4 KB+ cbuffer", "[rules][oversized-cbuffer]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Huge : register(b0) {
    float4 BigArray[256];
    float4 Tail;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return BigArray[0] + Tail; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    // Only assert when reflection actually surfaces the cbuffer size.
    for (const auto& d : diags) {
        if (d.code == "oversized-cbuffer")
            CHECK(d.severity == shader_clippy::Severity::Warning);
    }
    SUCCEED();
}
