// Tests for the manual-f32tof16 rule (Phase 7 Pack Precision; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_manual_f32tof16();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("mft.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_manual_f32tof16());
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

TEST_CASE("manual-f32tof16 fires on the canonical bit-twiddle pattern",
          "[rules][manual-f32tof16]") {
    const std::string hlsl = R"hlsl(
uint manual_convert(float x) {
    uint h = (asuint(x) >> 13) & 0x7FFF;
    return h;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "manual-f32tof16"));
}

TEST_CASE("manual-f32tof16 silent on a plain `f32tof16` call", "[rules][manual-f32tof16]") {
    const std::string hlsl = R"hlsl(
uint native_convert(float x) {
    return f32tof16(x);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "manual-f32tof16"));
}

TEST_CASE("manual-f32tof16 silent on unrelated bit work", "[rules][manual-f32tof16]") {
    const std::string hlsl = R"hlsl(
uint not_a_convert(uint x) {
    return (x >> 4) & 0x000F;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "manual-f32tof16"));
}
