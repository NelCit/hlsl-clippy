// End-to-end tests for numthreads-too-small.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("nts.hlsl", hlsl);
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

TEST_CASE("numthreads-too-small fires on 4x4x1", "[rules][numthreads-too-small]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(4, 4, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) { (void)dt; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "numthreads-too-small"));
}

TEST_CASE("numthreads-too-small does not fire on 8x8x1", "[rules][numthreads-too-small]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(8, 8, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) { (void)dt; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "numthreads-too-small"));
}
