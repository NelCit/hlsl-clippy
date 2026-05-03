// Tests for groupshared-over-32k-without-attribute (Phase 8 v0.8 pack; ADR 0018).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_over_32k_without_attribute();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("gs32k.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_groupshared_over_32k_without_attribute());
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

TEST_CASE("groupshared-over-32k-without-attribute fires when total > 32 KB and no attribute",
          "[rules][groupshared-over-32k-without-attribute]") {
    // 9000 floats * 4 bytes = 36 KB groupshared.
    const std::string hlsl = R"hlsl(
groupshared float lds[9000];

[numthreads(64, 1, 1)]
void cs_main() {
    lds[0] = 0.0f;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-over-32k-without-attribute"));
}

TEST_CASE("groupshared-over-32k-without-attribute silent under 32 KB",
          "[rules][groupshared-over-32k-without-attribute]") {
    // 1024 floats * 4 bytes = 4 KB.
    const std::string hlsl = R"hlsl(
groupshared float lds[1024];

[numthreads(64, 1, 1)]
void cs_main() {
    lds[0] = 0.0f;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "groupshared-over-32k-without-attribute"));
}

TEST_CASE("groupshared-over-32k-without-attribute silent when attribute is present",
          "[rules][groupshared-over-32k-without-attribute]") {
    const std::string hlsl = R"hlsl(
groupshared float lds[9000];

[GroupSharedLimit(65536)]
[numthreads(64, 1, 1)]
void cs_main() {
    lds[0] = 0.0f;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "groupshared-over-32k-without-attribute"));
}
