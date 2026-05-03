// End-to-end tests for the loop-attribute-conflict rule.
// Detects [unroll]+[loop] on the same loop, or [unroll(N)] with N over threshold.

#include <string>
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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("loop-attribute-conflict fires on [unroll][loop] on the same for",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() {
    [unroll][loop] for (int i = 0; i < 8; ++i) { }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "loop-attribute-conflict"));
}

TEST_CASE("loop-attribute-conflict fires on [unroll] [loop] on a while",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f(int n) {
    [unroll] [loop] while (n > 0) { --n; }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "loop-attribute-conflict"));
}

TEST_CASE("loop-attribute-conflict fires on [unroll(64)] (over threshold)",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() {
    [unroll(64)] for (int i = 0; i < 64; ++i) { }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "loop-attribute-conflict"));
}

TEST_CASE("loop-attribute-conflict does not fire on [unroll(8)] (under threshold)",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() {
    [unroll(8)] for (int i = 0; i < 8; ++i) { }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "loop-attribute-conflict");
}

TEST_CASE("loop-attribute-conflict does not fire on plain [unroll]",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() {
    [unroll] for (int i = 0; i < 8; ++i) { }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "loop-attribute-conflict");
}

TEST_CASE("loop-attribute-conflict does not fire on plain for",
          "[rules][loop-attribute-conflict]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
void f() {
    for (int i = 0; i < 8; ++i) { }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "loop-attribute-conflict");
}
