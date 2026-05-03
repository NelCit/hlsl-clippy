// End-to-end tests for the wave-active-all-equal-precheck rule.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

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
