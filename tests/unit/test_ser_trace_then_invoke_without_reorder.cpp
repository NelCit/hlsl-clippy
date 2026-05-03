// Tests for the ser-trace-then-invoke-without-reorder rule.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_ser_trace_then_invoke_without_reorder();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_ser_trace_then_invoke_without_reorder());
    return lint(sources, src, rules);
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

TEST_CASE("ser-trace-then-invoke-without-reorder fires on TraceRay then Invoke",
          "[rules][ser-trace-then-invoke-without-reorder]") {
    const std::string hlsl = R"hlsl(
void f() {
    dx::HitObject hit = HitObject::TraceRay(g_BVH, 0, 0xFF, 0, 1, 0, ray, p);
    hit.Invoke(p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "ser-trace-then-invoke-without-reorder"));
}

TEST_CASE("ser-trace-then-invoke-without-reorder is silent when MaybeReorderThread is present",
          "[rules][ser-trace-then-invoke-without-reorder]") {
    const std::string hlsl = R"hlsl(
void f() {
    dx::HitObject hit = HitObject::TraceRay(g_BVH, 0, 0xFF, 0, 1, 0, ray, p);
    MaybeReorderThread(hit);
    hit.Invoke(p);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "ser-trace-then-invoke-without-reorder");
    }
}
