// End-to-end tests for the byteaddressbuffer-narrow-when-typed-fits rule.
// Stage::Reflection -- needs Slang reflection to confirm the receiver
// identifier is bound to a (RW)ByteAddressBuffer before flagging the
// `asfloat(buf.Load4(...))` typed-cache pattern.

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
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_byteaddressbuffer_narrow_when_typed_fits();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rule() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_byteaddressbuffer_narrow_when_typed_fits());
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
    SourceManager sources;
    const std::string hlsl = R"hlsl(
ByteAddressBuffer raw_buf;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = asfloat4(raw_buf.Load4(16));
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
