// End-to-end tests for nodeid-implicit-mismatch.

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
    const auto src = sources.add_buffer("nim.hlsl", hlsl);
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

TEST_CASE("nodeid-implicit-mismatch fires on bare NodeOutput",
          "[rules][nodeid-implicit-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct DownstreamRecord { uint x; };

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(1, 1, 1)]
void Producer(NodeOutput<DownstreamRecord> Out) { (void)Out; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "nodeid-implicit-mismatch"));
}

TEST_CASE("nodeid-implicit-mismatch is silent when [NodeId] is present",
          "[rules][nodeid-implicit-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct DownstreamRecord { uint x; };

[Shader("node")]
[NodeLaunch("broadcasting")]
[NumThreads(1, 1, 1)]
void Producer([NodeId("explicit-name")] NodeOutput<DownstreamRecord> Out) { (void)Out; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "nodeid-implicit-mismatch"));
}
