// Tests for the long-vector-bytebuf-load-misaligned rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_bytebuf_load_misaligned();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_long_vector_bytebuf_load_misaligned());
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

TEST_CASE("long-vector-bytebuf-load-misaligned fires on offset 12",
          "[rules][long-vector-bytebuf-load-misaligned]") {
    const std::string hlsl = R"hlsl(
void f(ByteAddressBuffer g_Data) {
    vector<float, 8> v = g_Data.Load<vector<float, 8> >(12);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "long-vector-bytebuf-load-misaligned"));
}

TEST_CASE("long-vector-bytebuf-load-misaligned silent on offset 32",
          "[rules][long-vector-bytebuf-load-misaligned]") {
    const std::string hlsl = R"hlsl(
void f(ByteAddressBuffer g_Data) {
    vector<float, 8> v = g_Data.Load<vector<float, 8> >(32);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "long-vector-bytebuf-load-misaligned");
    }
}
