// Tests for the long-vector-non-elementwise-intrinsic rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_non_elementwise_intrinsic();
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
    rules.push_back(hlsl_clippy::rules::make_long_vector_non_elementwise_intrinsic());
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

TEST_CASE("long-vector-non-elementwise-intrinsic fires on length(vector<float, 8>)",
          "[rules][long-vector-non-elementwise-intrinsic]") {
    // The pure-AST stub matches when the call expression's source text
    // mentions a long-vector type (`vector<T, N>{>=5}` / `floatN{>=5}`); it
    // does not yet cross-reference parameter declarations. Use a constructor
    // call inside the argument so the long-vector type appears textually.
    const std::string hlsl = R"hlsl(
float f() {
    return length(vector<float, 8>(1, 2, 3, 4, 5, 6, 7, 8));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "long-vector-non-elementwise-intrinsic"));
}

TEST_CASE("long-vector-non-elementwise-intrinsic silent on length(float3)",
          "[rules][long-vector-non-elementwise-intrinsic]") {
    const std::string hlsl = R"hlsl(
float f(float3 v) {
    return length(v);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "long-vector-non-elementwise-intrinsic");
    }
}
