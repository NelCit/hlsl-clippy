// Unit tests for the inline-suppression scanner.

#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "shader_clippy/suppress.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;
using shader_clippy::SuppressionSet;

}  // namespace

TEST_CASE("scan parses a single line-scoped allow marker", "[suppress][scan]") {
    const std::string src =
        "// shader-clippy: allow(redundant-saturate)\n"
        "float3 f(float3 c) { return saturate(saturate(c)); }\n";

    const auto set = SuppressionSet::scan(src);
    REQUIRE(set.entries().size() == 1U);
    CHECK(set.entries()[0].rule_id == "redundant-saturate");

    // The byte range should cover the entire next line.
    const auto next_line_start = src.find("float3 f");
    REQUIRE(next_line_start != std::string::npos);
    CHECK(set.entries()[0].byte_lo == next_line_start);
    CHECK(set.entries()[0].byte_hi >= next_line_start);

    // suppresses() returns true for any byte within the next line.
    const auto saturate_pos = src.find("saturate", next_line_start);
    REQUIRE(saturate_pos != std::string::npos);
    CHECK(set.suppresses("redundant-saturate",
                         static_cast<std::uint32_t>(saturate_pos),
                         static_cast<std::uint32_t>(saturate_pos + 8U)));
    // A different rule is not suppressed.
    CHECK_FALSE(set.suppresses("other-rule",
                               static_cast<std::uint32_t>(saturate_pos),
                               static_cast<std::uint32_t>(saturate_pos + 1U)));
}

TEST_CASE("scan parses block-scoped allow markers", "[suppress][scan][block]") {
    const std::string src =
        "// shader-clippy: allow(redundant-saturate)\n"
        "{\n"
        "    saturate(saturate(c));\n"
        "    saturate(saturate(d));\n"
        "}\n"
        "saturate(saturate(e));\n";

    const auto set = SuppressionSet::scan(src);
    REQUIRE(set.entries().size() == 1U);

    const auto open_brace = src.find('{');
    const auto close_brace = src.find('}');
    REQUIRE(open_brace != std::string::npos);
    REQUIRE(close_brace != std::string::npos);
    CHECK(set.entries()[0].byte_lo == open_brace);
    CHECK(set.entries()[0].byte_hi == close_brace + 1U);

    // Inside the block: suppressed.
    const auto inside = src.find("saturate(saturate(c))");
    REQUIRE(inside != std::string::npos);
    CHECK(set.suppresses("redundant-saturate",
                         static_cast<std::uint32_t>(inside),
                         static_cast<std::uint32_t>(inside + 4U)));

    // Outside the block: not suppressed.
    const auto outside = src.find("saturate(saturate(e))");
    REQUIRE(outside != std::string::npos);
    CHECK_FALSE(set.suppresses("redundant-saturate",
                               static_cast<std::uint32_t>(outside),
                               static_cast<std::uint32_t>(outside + 4U)));
}

TEST_CASE("scan parses comma-separated rule lists", "[suppress][scan]") {
    const std::string src =
        "// shader-clippy: allow(rule-a, rule-b, rule-c)\n"
        "float x = 1.0;\n";

    const auto set = SuppressionSet::scan(src);
    REQUIRE(set.entries().size() == 3U);
    CHECK(set.entries()[0].rule_id == "rule-a");
    CHECK(set.entries()[1].rule_id == "rule-b");
    CHECK(set.entries()[2].rule_id == "rule-c");
}

TEST_CASE("scan recognises the wildcard form", "[suppress][scan][wildcard]") {
    const std::string src =
        "// shader-clippy: allow(*)\n"
        "saturate(saturate(c));\n";

    const auto set = SuppressionSet::scan(src);
    REQUIRE(set.entries().size() == 1U);
    CHECK(set.entries()[0].rule_id == "*");

    const auto pos = src.find("saturate(saturate");
    REQUIRE(pos != std::string::npos);
    CHECK(set.suppresses(
        "any-rule", static_cast<std::uint32_t>(pos), static_cast<std::uint32_t>(pos + 8U)));
    CHECK(set.suppresses("redundant-saturate",
                         static_cast<std::uint32_t>(pos),
                         static_cast<std::uint32_t>(pos + 8U)));
}

TEST_CASE("scan skips suppression markers inside string literals", "[suppress][scan][strings]") {
    // The scanner does NOT process // inside a string. We verify there's no
    // crash and no false annotation when a marker-shaped literal is embedded.
    const std::string src =
        "const char* msg = \"// shader-clippy: allow(rule-a)\";\n"
        "saturate(saturate(c));\n";

    const auto set = SuppressionSet::scan(src);
    CHECK(set.entries().empty());
}

TEST_CASE("scan emits a diagnostic on malformed marker", "[suppress][scan][malformed]") {
    const std::string src =
        "// shader-clippy: warn(rule-a)\n"
        "saturate(c);\n";

    const auto set = SuppressionSet::scan(src);
    CHECK(set.entries().empty());
    REQUIRE(set.scan_diagnostics().size() == 1U);
}

TEST_CASE("end-to-end: allow(redundant-saturate) suppresses the diagnostic",
          "[suppress][integration]") {
    SourceManager sources;
    const std::string hlsl =
        "// shader-clippy: allow(redundant-saturate)\n"
        "float3 f(float3 c) { return saturate(saturate(c)); }\n";

    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    // No `redundant-saturate` should be emitted (the allow line takes
    // precedence). Other rules in the default pack must not fire here either.
    for (const auto& d : diagnostics) {
        CHECK(d.code != "redundant-saturate");
    }
}

TEST_CASE("end-to-end: file-scope allow(*) at top of file", "[suppress][integration][wildcard]") {
    SourceManager sources;
    const std::string hlsl =
        "// shader-clippy: allow(*)\n"
        "float3 f(float3 c) {\n"
        "    return saturate(saturate(c));\n"
        "}\n";

    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);
    CHECK(diagnostics.empty());
}
