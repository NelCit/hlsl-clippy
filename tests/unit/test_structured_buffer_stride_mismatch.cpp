// End-to-end tests for the structured-buffer-stride-mismatch rule.

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
    const auto src = sources.add_buffer("sbs.hlsl", hlsl);
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

TEST_CASE("structured-buffer-stride-mismatch fires on a 12-byte struct",
          "[rules][structured-buffer-stride-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Particle {
    float3 pos;
};
StructuredBuffer<Particle> Particles;
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "structured-buffer-stride-mismatch"));
}

TEST_CASE("structured-buffer-stride-mismatch does not fire on a 16-byte struct",
          "[rules][structured-buffer-stride-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Particle {
    float4 pos;
};
StructuredBuffer<Particle> Particles;
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "structured-buffer-stride-mismatch"));
}
