// Tests for groupshared-stride-non-32-bank-conflict.
//
// Stage::Ast: detects `gs[tid * S]` where S is a partial-conflict stride
// (2, 4, 8, 16, 64) on 32-bank LDS.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_stride_non_32_bank_conflict();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_groupshared_stride_non_32_bank_conflict());
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

TEST_CASE("groupshared-stride-non-32-bank-conflict fires on stride 4 access",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared float g_PerThread[256];
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_PerThread[gi * 4 + 0] = (float)gi;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-stride-non-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-non-32-bank-conflict fires on stride 2 read",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[128];
void cs(uint tid) {
    float v = g_Tile[tid * 2];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-stride-non-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-non-32-bank-conflict fires on stride 8",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Tile[256];
void cs(uint dtid) {
    uint v = g_Tile[dtid * 8];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-stride-non-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-non-32-bank-conflict fires on stride 16",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared uint g_Tile[256];
void cs(uint gi) {
    g_Tile[gi * 16] = gi;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-stride-non-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-non-32-bank-conflict fires on shift form tid lshift 2",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[256];
void cs(uint tid) {
    float v = g_Tile[tid << 2];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "groupshared-stride-non-32-bank-conflict"));
}

TEST_CASE("groupshared-stride-non-32-bank-conflict silent on stride 1 contiguous",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
void cs(uint gi) {
    g_Tile[gi] = (float)gi;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-stride-non-32-bank-conflict");
    }
}

TEST_CASE("groupshared-stride-non-32-bank-conflict silent on stride 3 (coprime with 32)",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[256];
void cs(uint gi) {
    g_Tile[gi * 3] = (float)gi;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-stride-non-32-bank-conflict");
    }
}

TEST_CASE("groupshared-stride-non-32-bank-conflict silent on non-groupshared array",
          "[rules][groupshared-stride-non-32-bank-conflict]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> Out;
void cs(uint gi) {
    Out[gi * 4] = (float)gi;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "groupshared-stride-non-32-bank-conflict");
    }
}
