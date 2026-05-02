// Tests for numwaves-anchored-cap (Phase 8 deferred; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_numwaves_anchored_cap();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("nwac.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_numwaves_anchored_cap());
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

TEST_CASE("numwaves-anchored-cap fires when total > 1024",
          "[rules][numwaves-anchored-cap]") {
    const std::string hlsl = R"hlsl(
[numthreads(64, 32, 1)]
void cs_main() {}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "numwaves-anchored-cap"));
}

TEST_CASE("numwaves-anchored-cap silent at 1024",
          "[rules][numwaves-anchored-cap]") {
    const std::string hlsl = R"hlsl(
[numthreads(32, 32, 1)]
void cs_main() {}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "numwaves-anchored-cap"));
}
