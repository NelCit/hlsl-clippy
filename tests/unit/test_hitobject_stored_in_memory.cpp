// Tests for the hitobject-stored-in-memory SM 6.9 SER rule. The rule isn't
// wired into the default registry yet (Phase 3 wiring agent splices later);
// tests construct the rule directly via its factory.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_stored_in_memory();
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
    rules.push_back(shader_clippy::rules::make_hitobject_stored_in_memory());
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

TEST_CASE("hitobject-stored-in-memory fires on groupshared HitObject array",
          "[rules][hitobject-stored-in-memory]") {
    const std::string hlsl = R"hlsl(
groupshared dx::HitObject s_hits[64];
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "hitobject-stored-in-memory"));
}

TEST_CASE("hitobject-stored-in-memory does not fire on a register-only local",
          "[rules][hitobject-stored-in-memory]") {
    const std::string hlsl = R"hlsl(
void f() {
    dx::HitObject hit;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "hitobject-stored-in-memory");
    }
}
