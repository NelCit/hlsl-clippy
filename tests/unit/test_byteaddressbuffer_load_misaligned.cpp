// End-to-end tests for the byteaddressbuffer-load-misaligned rule.
// Stage::Reflection -- needs a Slang reflection pass to confirm the receiver
// identifier is bound to a (RW)ByteAddressBuffer before firing on a
// misaligned constant offset.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_byteaddressbuffer_load_misaligned();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_byteaddressbuffer_load_misaligned());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rule();
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("byteaddressbuffer-load-misaligned fires on Load4 at offset 4",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint4 v = raw_buf.Load4(4);
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-load-misaligned"));
}

TEST_CASE("byteaddressbuffer-load-misaligned fires on Load2 at offset 4",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 v = raw_buf.Load2(4);
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-load-misaligned"));
}

TEST_CASE("byteaddressbuffer-load-misaligned does not fire on Load4 at offset 16",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint4 v = raw_buf.Load4(16);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-load-misaligned");
}

TEST_CASE("byteaddressbuffer-load-misaligned does not fire on Load2 at offset 8",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 v = raw_buf.Load2(8);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-load-misaligned");
}

TEST_CASE("byteaddressbuffer-load-misaligned does not fire on dynamic offset",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint4 v = raw_buf.Load4(tid.x * 16);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-load-misaligned");
}

TEST_CASE("byteaddressbuffer-load-misaligned does not fire on a typed buffer Load",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Buffer<float4> typed_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = typed_buf.Load(2);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-load-misaligned");
}

TEST_CASE("byteaddressbuffer-load-misaligned fires on RWByteAddressBuffer Load2 at offset 6",
          "[rules][byteaddressbuffer-load-misaligned]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWByteAddressBuffer rw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint2 v = rw_buf.Load2(6);
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-load-misaligned"));
}
