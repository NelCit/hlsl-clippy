// Tests for the groupshared-when-registers-suffice rule (Phase 7 Pack
// Pressure; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_when_registers_suffice();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("gwrs.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_groupshared_when_registers_suffice());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
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

TEST_CASE("groupshared-when-registers-suffice fires on small per-thread arrays",
          "[rules][groupshared-when-registers-suffice]") {
    const std::string hlsl = R"hlsl(
groupshared float arr[4];

[numthreads(4, 1, 1)]
void cs_main(uint gi : SV_GroupIndex) {
    arr[gi] = (float)gi;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-when-registers-suffice"));
}

TEST_CASE("groupshared-when-registers-suffice silent on large arrays",
          "[rules][groupshared-when-registers-suffice]") {
    const std::string hlsl = R"hlsl(
groupshared float big_arr[256];

[numthreads(64, 1, 1)]
void cs_main(uint gi : SV_GroupIndex) {
    big_arr[gi] = (float)gi;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "groupshared-when-registers-suffice"));
}
