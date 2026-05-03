// End-to-end tests for the interlocked-float-bit-cast-trick rule.

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
    const auto src = sources.add_buffer("ifbct.hlsl", hlsl);
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

TEST_CASE("interlocked-float-bit-cast-trick fires on InterlockedMin(asuint(x))",
          "[rules][interlocked-float-bit-cast-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> minBuf;

void atomic_min_float(float v) {
    InterlockedMin(minBuf[0], asuint(v));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "interlocked-float-bit-cast-trick"));
}

TEST_CASE("interlocked-float-bit-cast-trick does not fire on plain InterlockedAdd",
          "[rules][interlocked-float-bit-cast-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> Counter;

void inc() { InterlockedAdd(Counter[0], 1); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "interlocked-float-bit-cast-trick");
}
