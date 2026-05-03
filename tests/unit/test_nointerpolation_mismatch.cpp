// End-to-end tests for nointerpolation-mismatch.

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
    const auto src = sources.add_buffer("nim2.hlsl", hlsl);
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

TEST_CASE("nointerpolation-mismatch fires on uint TEXCOORD", "[rules][nointerpolation-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct VsOut {
    float4 pos    : SV_Position;
    uint   matId  : TEXCOORD0;
};
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "nointerpolation-mismatch"));
}

TEST_CASE("nointerpolation-mismatch silent on nointerpolation uint",
          "[rules][nointerpolation-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct VsOut {
    float4               pos    : SV_Position;
    nointerpolation uint matId  : TEXCOORD0;
};
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "nointerpolation-mismatch"));
}
