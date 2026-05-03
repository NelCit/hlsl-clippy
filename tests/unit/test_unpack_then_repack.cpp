// Tests for the unpack-then-repack rule (Phase 7 Pack Precision; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_unpack_then_repack();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("utr.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_unpack_then_repack());
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

TEST_CASE("unpack-then-repack fires on f32tof16(f16tof32(x))", "[rules][unpack-then-repack]") {
    const std::string hlsl = R"hlsl(
uint round_trip(uint h) {
    return f32tof16(f16tof32(h));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "unpack-then-repack"));
}

TEST_CASE("unpack-then-repack fires on pack_u8(unpack_u8u32(x))", "[rules][unpack-then-repack]") {
    const std::string hlsl = R"hlsl(
uint round_trip(uint x) {
    return pack_u8(unpack_u8u32(x));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "unpack-then-repack"));
}

TEST_CASE("unpack-then-repack silent when there is intermediate ALU",
          "[rules][unpack-then-repack]") {
    const std::string hlsl = R"hlsl(
uint with_modify(uint x) {
    uint4 u = unpack_u8u32(x);
    u.r = u.r >> 1u;
    return pack_u8(u);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "unpack-then-repack");
}
