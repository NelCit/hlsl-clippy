// Tests for getgroupwaveindex-without-wavesize-attribute (Phase 8 v0.8 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_getgroupwaveindex_without_wavesize_attribute();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("ggwi.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_getgroupwaveindex_without_wavesize_attribute());
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

TEST_CASE("getgroupwaveindex-without-wavesize-attribute fires when [WaveSize] is missing",
          "[rules][getgroupwaveindex-without-wavesize-attribute]") {
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) {
    uint i = GetGroupWaveIndex();
    (void)i;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "getgroupwaveindex-without-wavesize-attribute"));
}

TEST_CASE("getgroupwaveindex-without-wavesize-attribute silent with [WaveSize(32)]",
          "[rules][getgroupwaveindex-without-wavesize-attribute]") {
    const std::string hlsl = R"hlsl(
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) {
    uint i = GetGroupWaveIndex();
    (void)i;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "getgroupwaveindex-without-wavesize-attribute"));
}
