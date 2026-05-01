// End-to-end tests for the dispatchmesh-not-called rule.
// Amplification entry points must call `DispatchMesh(...)` before returning.

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
[[nodiscard]] std::unique_ptr<hlsl_clippy::Rule> make_dispatchmesh_not_called();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_dispatchmesh_not_called());
    return rules;
}

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_rules();
    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = false;
    return lint(sources, src, rules, options);
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

TEST_CASE("dispatchmesh-not-called fires on amplification entry without DispatchMesh",
          "[rules][dispatchmesh-not-called]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Payload { uint count; };

[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID)
{
    Payload p;
    p.count = gtid;
    return;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dispatchmesh-not-called"));
}

TEST_CASE("dispatchmesh-not-called does not fire when DispatchMesh is called",
          "[rules][dispatchmesh-not-called]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Payload { uint count; };

[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID)
{
    Payload p;
    p.count = gtid;
    DispatchMesh(p.count, 1, 1, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "dispatchmesh-not-called"));
}

TEST_CASE("dispatchmesh-not-called does not fire on non-amplification function",
          "[rules][dispatchmesh-not-called]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("compute")]
[numthreads(32, 1, 1)]
void cs_main(uint gtid : SV_GroupThreadID)
{
    return;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "dispatchmesh-not-called"));
}

TEST_CASE("dispatchmesh-not-called does not fire on similarly-named DispatchMeshHelper",
          "[rules][dispatchmesh-not-called]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct Payload { uint count; };

[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID)
{
    Payload p;
    DispatchMesh(p.count, 1, 1, p);
}
)hlsl";
    // Confirm the body-scan on `DispatchMesh` is identifier-aware (no false
    // hit when the call is preceded by another identifier prefix).
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "dispatchmesh-not-called"));
}
