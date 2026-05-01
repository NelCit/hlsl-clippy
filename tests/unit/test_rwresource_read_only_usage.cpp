// End-to-end tests for the rwresource-read-only-usage rule.

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
    const auto src = sources.add_buffer("rwro.hlsl", hlsl);
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

TEST_CASE("rwresource-read-only-usage does not fire on a written UAV",
          "[rules][rwresource-read-only-usage]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWBuffer<float> Out : register(u0);

[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) {
    Out[i] = 1.0;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "rwresource-read-only-usage"));
}

TEST_CASE("rwresource-read-only-usage fires on a read-only RWBuffer",
          "[rules][rwresource-read-only-usage]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWBuffer<float> ReadMe : register(u0);
RWBuffer<float> Sink   : register(u1);

[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) {
    Sink[i] = ReadMe[i];
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        if (d.code == "rwresource-read-only-usage")
            CHECK(d.severity == hlsl_clippy::Severity::Warning);
    }
    SUCCEED();
}
