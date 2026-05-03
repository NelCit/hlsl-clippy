// End-to-end tests for the small-loop-no-unroll rule.

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
    const auto src = sources.add_buffer("smlu.hlsl", hlsl);
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

TEST_CASE("small-loop-no-unroll fires on for(i=0; i<4; ...) without [unroll]",
          "[rules][small-loop-no-unroll]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float sum_four() {
    float acc = 0.0;
    for (int i = 0; i < 4; ++i) {
        acc = acc + 1.0;
    }
    return acc;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "small-loop-no-unroll"));
}

TEST_CASE("small-loop-no-unroll does not fire when [unroll] is present",
          "[rules][small-loop-no-unroll]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float sum_four() {
    float acc = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        acc = acc + 1.0;
    }
    return acc;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "small-loop-no-unroll");
}

TEST_CASE("small-loop-no-unroll does not fire on large bound", "[rules][small-loop-no-unroll]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float sum_many() {
    float acc = 0.0;
    for (int i = 0; i < 64; ++i) {
        acc = acc + 1.0;
    }
    return acc;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "small-loop-no-unroll");
}
