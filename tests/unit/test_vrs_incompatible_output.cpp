// End-to-end tests for vrs-incompatible-output.

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
    const auto src = sources.add_buffer("vrsio.hlsl", hlsl);
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

TEST_CASE("vrs-incompatible-output fires on discard in pixel shader",
          "[rules][vrs-incompatible-output]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(float4 pos : SV_Position) : SV_Target {
    if (pos.x < 0.5) discard;
    return float4(0, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "vrs-incompatible-output"));
}

TEST_CASE("vrs-incompatible-output silent on plain pixel shader",
          "[rules][vrs-incompatible-output]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "vrs-incompatible-output"));
}
