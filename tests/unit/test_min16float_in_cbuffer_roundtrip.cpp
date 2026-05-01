// End-to-end tests for min16float-in-cbuffer-roundtrip.

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
using hlsl_clippy::LintOptions;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("m16cbr.hlsl", hlsl);
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

TEST_CASE("min16float-in-cbuffer-roundtrip silent on a float-only cbuffer",
          "[rules][min16float-in-cbuffer-roundtrip]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer F : register(b0) {
    float4 a;
    float  t;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return a + t; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "min16float-in-cbuffer-roundtrip"));
}
