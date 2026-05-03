// Tests for the buffer-load-width-vs-cache-line rule (Phase 7 Pack Pressure;
// ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_buffer_load_width_vs_cache_line();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("blwcl.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_buffer_load_width_vs_cache_line());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("buffer-load-width-vs-cache-line fires on 4 scalar Loads in a 16-byte window",
          "[rules][buffer-load-width-vs-cache-line]") {
    const std::string hlsl = R"hlsl(
ByteAddressBuffer Buf;

uint4 cs_main() {
    uint a = Buf.Load(0);
    uint b = Buf.Load(4);
    uint c = Buf.Load(8);
    uint d = Buf.Load(12);
    return uint4(a, b, c, d);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "buffer-load-width-vs-cache-line"));
}

TEST_CASE("buffer-load-width-vs-cache-line silent on widely-spaced loads",
          "[rules][buffer-load-width-vs-cache-line]") {
    const std::string hlsl = R"hlsl(
ByteAddressBuffer Buf;

uint2 cs_main() {
    uint a = Buf.Load(0);
    uint b = Buf.Load(64);
    return uint2(a, b);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "buffer-load-width-vs-cache-line"));
}
