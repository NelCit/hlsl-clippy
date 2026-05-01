// End-to-end tests for pack-clamp-on-prove-bounded.

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
    const auto src = sources.add_buffer("pcb.hlsl", hlsl);
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

TEST_CASE("pack-clamp-on-prove-bounded fires when input is saturated",
          "[rules][pack-clamp-on-prove-bounded]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) {
    float4 v = float4(0.5, 0.5, 0.5, 0.5);
    uint packed = pack_clamp_u8(saturate(v) * 255.0);
    (void)packed;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pack-clamp-on-prove-bounded"));
}

TEST_CASE("pack-clamp-on-prove-bounded silent on bare pack_clamp",
          "[rules][pack-clamp-on-prove-bounded]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_main(uint i : SV_DispatchThreadID) {
    float4 v = float4(i, i + 1, i + 2, i + 3);
    uint packed = pack_clamp_u8(v);
    (void)packed;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "pack-clamp-on-prove-bounded"));
}
