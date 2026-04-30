// Unit tests for the quick-fix Rewriter.

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rewriter.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::ByteSpan;
using hlsl_clippy::Fix;
using hlsl_clippy::FixConflict;
using hlsl_clippy::FixPriority;
using hlsl_clippy::PrioritisedFix;
using hlsl_clippy::Rewriter;
using hlsl_clippy::Severity;
using hlsl_clippy::SourceId;
using hlsl_clippy::Span;
using hlsl_clippy::TextEdit;

[[nodiscard]] PrioritisedFix make_fix(std::string rule_id,
                                      Severity sev,
                                      std::uint32_t lo,
                                      std::uint32_t hi,
                                      std::string replacement) {
    PrioritisedFix pf;
    pf.priority = FixPriority{.severity = sev, .rule_id = std::move(rule_id)};
    Fix fix;
    fix.machine_applicable = true;
    TextEdit edit;
    edit.span = Span{.source = SourceId{1U}, .bytes = ByteSpan{.lo = lo, .hi = hi}};
    edit.replacement = std::move(replacement);
    fix.edits.push_back(std::move(edit));
    pf.fix = std::move(fix);
    return pf;
}

}  // namespace

TEST_CASE("Rewriter::apply on empty input returns the source unchanged", "[rewriter]") {
    const Rewriter rw;
    const std::string out = rw.apply("hello", {});
    CHECK(out == "hello");
}

TEST_CASE("Rewriter::apply applies disjoint single-edit fixes", "[rewriter][disjoint]") {
    const Rewriter rw;
    const std::string source = "abcdefghij";

    std::vector<PrioritisedFix> fixes;
    fixes.push_back(make_fix("rule-a", Severity::Warning, 0U, 3U, "XYZ"));
    fixes.push_back(make_fix("rule-b", Severity::Warning, 5U, 7U, "QQ"));

    const std::string out = rw.apply(source, fixes);
    CHECK(out == "XYZdeQQhij");
}

TEST_CASE("Rewriter::apply drops the lower-priority overlapping fix", "[rewriter][conflict]") {
    const Rewriter rw;
    const std::string source = "abcdefghij";

    std::vector<PrioritisedFix> fixes;
    // Both target [2, 6); higher severity wins.
    fixes.push_back(make_fix("low", Severity::Warning, 2U, 6U, "low!"));
    fixes.push_back(make_fix("high", Severity::Error, 2U, 6U, "HIGH"));

    std::vector<FixConflict> conflicts;
    const std::string out = rw.apply(source, fixes, &conflicts);

    CHECK(out == "abHIGHghij");
    REQUIRE(conflicts.size() == 1U);
    CHECK(conflicts[0].dropped_rule_id == "low");
    CHECK(conflicts[0].winning_rule_id == "high");
}

TEST_CASE("Rewriter::apply breaks ties on rule_id deterministically", "[rewriter][conflict]") {
    const Rewriter rw;
    const std::string source = "abcdefghij";

    std::vector<PrioritisedFix> fixes;
    fixes.push_back(make_fix("rule-zzz", Severity::Warning, 2U, 6U, "Z"));
    fixes.push_back(make_fix("rule-aaa", Severity::Warning, 2U, 6U, "A"));
    // Equal severity: rule_id lexicographic ordering wins; "rule-aaa" < "rule-zzz".

    const std::string out = rw.apply(source, fixes);
    CHECK(out == "abAghij");
}

TEST_CASE("Rewriter::apply handles a multi-edit atomic fix", "[rewriter][multi-edit]") {
    const Rewriter rw;
    const std::string source = "abcdefghij";

    PrioritisedFix pf;
    pf.priority = FixPriority{.severity = Severity::Warning, .rule_id = "multi"};
    Fix fix;
    fix.machine_applicable = true;
    {
        TextEdit e;
        e.span = Span{.source = SourceId{1U}, .bytes = ByteSpan{.lo = 0U, .hi = 1U}};
        e.replacement = "Z";
        fix.edits.push_back(std::move(e));
    }
    {
        TextEdit e;
        e.span = Span{.source = SourceId{1U}, .bytes = ByteSpan{.lo = 9U, .hi = 10U}};
        e.replacement = "Y";
        fix.edits.push_back(std::move(e));
    }
    pf.fix = std::move(fix);

    std::vector<PrioritisedFix> fixes;
    fixes.push_back(std::move(pf));

    const std::string out = rw.apply(source, fixes);
    CHECK(out ==
          "Zbcdefghi"
          "Y");
}

TEST_CASE("Rewriter::apply drops every edit of a multi-edit fix when one conflicts",
          "[rewriter][multi-edit][atomic]") {
    const Rewriter rw;
    const std::string source = "abcdefghij";

    // High-priority single edit at [3, 5).
    std::vector<PrioritisedFix> fixes;
    fixes.push_back(make_fix("hi", Severity::Error, 3U, 5U, "!!"));

    // Low-priority multi-edit fix where one of its edits ([4, 5)) overlaps.
    PrioritisedFix lo;
    lo.priority = FixPriority{.severity = Severity::Warning, .rule_id = "lo"};
    Fix fix;
    {
        TextEdit e;
        e.span = Span{.source = SourceId{1U}, .bytes = ByteSpan{.lo = 0U, .hi = 1U}};
        e.replacement = "Z";
        fix.edits.push_back(std::move(e));
    }
    {
        TextEdit e;
        e.span = Span{.source = SourceId{1U}, .bytes = ByteSpan{.lo = 4U, .hi = 5U}};
        e.replacement = "Q";
        fix.edits.push_back(std::move(e));
    }
    lo.fix = std::move(fix);
    fixes.push_back(std::move(lo));

    std::vector<FixConflict> conflicts;
    const std::string out = rw.apply(source, fixes, &conflicts);

    // The "lo" fix is entirely dropped, including its [0, 1) edit.
    CHECK(out == "abc!!fghij");
    REQUIRE(conflicts.size() == 1U);
    CHECK(conflicts[0].dropped_rule_id == "lo");
}

TEST_CASE("Rewriter::apply is idempotent on already-clean input", "[rewriter][idempotent]") {
    const Rewriter rw;
    const std::string source = "saturate(c)";
    const std::string out = rw.apply(source, {});
    CHECK(out == source);
}
