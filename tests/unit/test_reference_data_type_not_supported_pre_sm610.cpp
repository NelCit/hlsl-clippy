// Tests for reference-data-type-not-supported-pre-sm610 (Phase 8 deferred; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_reference_data_type_not_supported_pre_sm610();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  const std::string& profile) {
    SourceManager sources;
    const auto src = sources.add_buffer("rdtnsp.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_reference_data_type_not_supported_pre_sm610());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = profile;
    return lint(sources, src, rules, opts);
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

TEST_CASE("reference-data-type-not-supported-pre-sm610 fires on SM 6.9 with `inout ref`",
          "[rules][reference-data-type-not-supported-pre-sm610]") {
    const std::string hlsl = R"hlsl(
void f(inout ref float v) { v = 0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, "sm_6_9"),
                   "reference-data-type-not-supported-pre-sm610"));
}

TEST_CASE("reference-data-type-not-supported-pre-sm610 silent without ref-typed param",
          "[rules][reference-data-type-not-supported-pre-sm610]") {
    const std::string hlsl = R"hlsl(
void f(inout float v) { v = 0; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, "sm_6_9"),
                         "reference-data-type-not-supported-pre-sm610"));
}
