// Tests for the long-vector-typed-buffer-load rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_typed_buffer_load();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_long_vector_typed_buffer_load());
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

TEST_CASE("long-vector-typed-buffer-load fires on Buffer<vector<float, 8>>",
          "[rules][long-vector-typed-buffer-load]") {
    const std::string hlsl = R"hlsl(
Buffer<vector<float, 8> > g_LongVecData;
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "long-vector-typed-buffer-load"));
}

TEST_CASE("long-vector-typed-buffer-load silent on Buffer<float4>",
          "[rules][long-vector-typed-buffer-load]") {
    const std::string hlsl = R"hlsl(
Buffer<float4> g_Data;
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "long-vector-typed-buffer-load");
    }
}

TEST_CASE("long-vector-typed-buffer-load silent on StructuredBuffer<vector<float, 8>>",
          "[rules][long-vector-typed-buffer-load]") {
    const std::string hlsl = R"hlsl(
StructuredBuffer<vector<float, 8> > g_LongVecData;
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "long-vector-typed-buffer-load");
    }
}
