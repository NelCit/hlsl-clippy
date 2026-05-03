// End-to-end tests for the byteaddressbuffer-narrow-when-typed-fits rule.
// Stage::Reflection -- needs Slang reflection to confirm the receiver
// identifier is bound to a (RW)ByteAddressBuffer before flagging the
// `asfloat(buf.Load4(...))` typed-cache pattern.

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
[[nodiscard]] std::unique_ptr<shader_clippy::Rule> make_byteaddressbuffer_narrow_when_typed_fits();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_byteaddressbuffer_narrow_when_typed_fits());
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

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits fires on asfloat(buf.Load4(K))",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = asfloat(raw_buf.Load4(0));
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-narrow-when-typed-fits"));
}

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits fires on asfloat4(buf.Load4)",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    // `asfloat4` is not a real HLSL intrinsic (the parametric overload of
    // `asfloat` covers 1/2/3/4 element vectors); using `asfloat` here keeps
    // the receiver-and-method shape under test while letting Slang accept the
    // source so reflection can confirm `raw_buf` is a ByteAddressBuffer.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = asfloat(raw_buf.Load4(16));
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-narrow-when-typed-fits"));
}

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits fires on asint(buf.Load)",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    int v = asint(raw_buf.Load(4));
    (void)v;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "byteaddressbuffer-narrow-when-typed-fits"));
}

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits does not fire on plain Load4 (no bit-cast)",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint4 v = raw_buf.Load4(0);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-narrow-when-typed-fits");
}

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits does not fire on a typed-buffer Load",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Buffer<uint4> typed_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = asfloat(typed_buf.Load(0));
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-narrow-when-typed-fits");
}

TEST_CASE("byteaddressbuffer-narrow-when-typed-fits does not fire on asfloat of a non-load",
          "[rules][byteaddressbuffer-narrow-when-typed-fits]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = asfloat(tid.x);
    (void)v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "byteaddressbuffer-narrow-when-typed-fits");
}
