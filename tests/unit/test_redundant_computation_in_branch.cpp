// End-to-end tests for the redundant-computation-in-branch rule.

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
    const auto src = sources.add_buffer("rcb.hlsl", hlsl);
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

TEST_CASE("redundant-computation-in-branch fires when both arms start identically",
          "[rules][redundant-computation-in-branch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float pick(float x, bool c) {
    float y;
    if (c) {
        y = x * x;
        y = y + 1.0;
    } else {
        y = x * x;
        y = y - 1.0;
    }
    return y;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-computation-in-branch"));
}

TEST_CASE("redundant-computation-in-branch does not fire when arms differ",
          "[rules][redundant-computation-in-branch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float pick(float x, bool c) {
    if (c) {
        return x * 2.0;
    } else {
        return x * 3.0;
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "redundant-computation-in-branch");
}
