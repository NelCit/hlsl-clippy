// Tests for the scratch-from-dynamic-indexing rule (Phase 7 Pack Pressure;
// ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_scratch_from_dynamic_indexing();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("sfdi.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_scratch_from_dynamic_indexing());
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

TEST_CASE("scratch-from-dynamic-indexing fires on a non-constant local-array index",
          "[rules][scratch-from-dynamic-indexing]") {
    const std::string hlsl = R"hlsl(
float4 ps_main(uint dyn : INDEX) : SV_Target {
    float4 lut[4];
    lut[0] = float4(1, 0, 0, 1);
    lut[1] = float4(0, 1, 0, 1);
    lut[2] = float4(0, 0, 1, 1);
    lut[3] = float4(1, 1, 0, 1);
    return lut[dyn & 3u];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "scratch-from-dynamic-indexing"));
}

TEST_CASE("scratch-from-dynamic-indexing silent on a constant index",
          "[rules][scratch-from-dynamic-indexing]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target {
    float4 lut[4];
    lut[0] = float4(1, 0, 0, 1);
    lut[1] = float4(0, 1, 0, 1);
    lut[2] = float4(0, 0, 1, 1);
    lut[3] = float4(1, 1, 0, 1);
    return lut[2];
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "scratch-from-dynamic-indexing"));
}
