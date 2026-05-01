// End-to-end tests for the wave-active-all-equal-precheck rule.

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
    const auto src = sources.add_buffer("wae.hlsl", hlsl);
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

TEST_CASE("wave-active-all-equal-precheck fires on raw ResourceDescriptorHeap[idx]",
          "[rules][wave-active-all-equal-precheck]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D get(uint idx) {
    return ResourceDescriptorHeap[idx];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "wave-active-all-equal-precheck"));
}

TEST_CASE("wave-active-all-equal-precheck does not fire when guarded by NonUniformResourceIndex",
          "[rules][wave-active-all-equal-precheck]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D get(uint idx) {
    return ResourceDescriptorHeap[NonUniformResourceIndex(idx)];
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "wave-active-all-equal-precheck");
}
