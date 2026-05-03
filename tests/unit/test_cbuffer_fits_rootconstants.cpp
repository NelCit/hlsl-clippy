// End-to-end tests for the cbuffer-fits-rootconstants rule.

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
    const auto src = sources.add_buffer("rootc.hlsl", hlsl);
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

TEST_CASE("cbuffer-fits-rootconstants is potentially emitted on small cbuffer",
          "[rules][cbuffer-fits-rootconstants]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Tiny : register(b2) {
    uint InstanceID;
    uint MaterialID;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return float4(InstanceID, MaterialID, 0, 0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        if (d.code == "cbuffer-fits-rootconstants")
            CHECK(d.severity == shader_clippy::Severity::Warning);
    }
    SUCCEED();
}

TEST_CASE("cbuffer-fits-rootconstants does not fire on a wide cbuffer",
          "[rules][cbuffer-fits-rootconstants]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Wide : register(b2) {
    float4 a;
    float4 b;
    float4 c;
    float4 d;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return a + b + c + d; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "cbuffer-fits-rootconstants"));
}
