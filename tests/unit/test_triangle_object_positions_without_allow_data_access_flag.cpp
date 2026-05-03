// Tests for triangle-object-positions-without-allow-data-access-flag.

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
[[nodiscard]] std::unique_ptr<Rule> make_triangle_object_positions_without_allow_data_access_flag();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("top.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(
        shader_clippy::rules::make_triangle_object_positions_without_allow_data_access_flag());
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

TEST_CASE("triangle-object-positions-without-allow-data-access-flag fires on every call site",
          "[rules][triangle-object-positions-without-allow-data-access-flag]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    float3 pos[3] = TriangleObjectPositions();
    (void)pos;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl),
                   "triangle-object-positions-without-allow-data-access-flag"));
}

TEST_CASE("triangle-object-positions-without-allow-data-access-flag silent without the call",
          "[rules][triangle-object-positions-without-allow-data-access-flag]") {
    const std::string hlsl = R"hlsl(
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    p.color = float3(1, 0, 0);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl),
                         "triangle-object-positions-without-allow-data-access-flag"));
}
