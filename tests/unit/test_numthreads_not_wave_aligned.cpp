// End-to-end tests for numthreads-not-wave-aligned.

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
    const auto src = sources.add_buffer("nwa.hlsl", hlsl);
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

TEST_CASE("numthreads-not-wave-aligned fires on 7x7x1", "[rules][numthreads-not-wave-aligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(7, 7, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) { (void)dt; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "numthreads-not-wave-aligned"));
}

TEST_CASE("numthreads-not-wave-aligned does not fire on 8x8x1",
          "[rules][numthreads-not-wave-aligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(8, 8, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) { (void)dt; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "numthreads-not-wave-aligned"));
}

TEST_CASE("numthreads-not-wave-aligned [fix] stub-burndown -- fires on canonical 65x1x1",
          "[rules][numthreads-not-wave-aligned][fix]") {
    // Stub-burndown verification per ADR 0018: the v0.5 stub-shape rule was
    // a no-op TU; the v0.8 implementation fires on a canonical wave-32-
    // unaligned shape (65 > 32 and 65 % 32 != 0).
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(65, 1, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) { (void)dt; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "numthreads-not-wave-aligned"));
}
