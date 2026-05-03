// Tests for the hitobject-passed-to-non-inlined-fn SER rule (Phase 3
// forward-compatible-stub: only catches the trivial [noinline] form).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_passed_to_non_inlined_fn();
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
    rules.push_back(shader_clippy::rules::make_hitobject_passed_to_non_inlined_fn());
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

TEST_CASE("hitobject-passed-to-non-inlined-fn fires on noinline with HitObject param",
          "[rules][hitobject-passed-to-non-inlined-fn]") {
    const std::string hlsl = R"hlsl(
[noinline]
void Helper(dx::HitObject hit) {
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "hitobject-passed-to-non-inlined-fn"));
}

TEST_CASE("hitobject-passed-to-non-inlined-fn ignores inline functions",
          "[rules][hitobject-passed-to-non-inlined-fn]") {
    const std::string hlsl = R"hlsl(
void Helper(dx::HitObject hit) {
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "hitobject-passed-to-non-inlined-fn");
    }
}
