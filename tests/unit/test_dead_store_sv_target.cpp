// End-to-end tests for dead-store-sv-target.

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
    const auto src = sources.add_buffer("dst.hlsl", hlsl);
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

TEST_CASE("dead-store-sv-target fires on duplicate same-scope writes",
          "[rules][dead-store-sv-target]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct PsOut {
    float4 color : SV_Target0;
};

[shader("pixel")]
PsOut ps_main() {
    PsOut o;
    o.color = float4(1, 0, 0, 1);
    o.color = float4(0, 1, 0, 1);
    return o;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dead-store-sv-target"));
}

TEST_CASE("dead-store-sv-target does not fire on a single write", "[rules][dead-store-sv-target]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct PsOut {
    float4 color : SV_Target0;
};

[shader("pixel")]
PsOut ps_main() {
    PsOut o;
    o.color = float4(0, 1, 0, 1);
    return o;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "dead-store-sv-target"));
}
