// Smoke + correctness tests for the Phase 7 liveness analysis under
// `core/src/rules/util/liveness.{hpp,cpp}` (ADR 0017 sub-phase 7b.1).
//
// Coverage:
//   * Simple use-after-def: `int x = 1; return x;` -> `x` is live across
//     the assignment (live_in of return-block contains "x").
//   * Branch merge: `if (cond) y = 1; else y = 2; return y;` -> `y` live
//     at exit of both branch arms (i.e. both arms' live_out contain "y").
//   * Loop carry: `for (int i = 0; i < n; ++i) sum += a[i];` -> `i` and
//     `sum` live across the loop back-edge (header block live_in contains
//     both names).
//   * Liveness across a function call: locals stay live across an opaque
//     callee.
//   * Dead-store regression: an assignment whose result is never read --
//     `live_out` for that block does NOT contain the dead name.
//   * Empty-CFG path: `compute_liveness` returns an empty info gracefully.

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "control_flow/cfg_storage.hpp"
#include "control_flow/engine.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"
#include "parser_internal.hpp"
#include "rules/util/liveness.hpp"

namespace {

using hlsl_clippy::AstTree;
using hlsl_clippy::BasicBlockId;
using hlsl_clippy::ByteSpan;
using hlsl_clippy::ControlFlowInfo;
using hlsl_clippy::SourceId;
using hlsl_clippy::SourceManager;
using hlsl_clippy::Span;

namespace util = hlsl_clippy::util;

struct Built {
    hlsl_clippy::parser::ParsedSource parsed;
    ControlFlowInfo cfg;
    SourceId source;
};

/// Parse + build the CFG using the same tree-sitter root, and return
/// both so the test can construct a matching `AstTree` for liveness.
[[nodiscard]] Built build(SourceManager& sources, const std::string& name, std::string_view src) {
    auto& engine = hlsl_clippy::control_flow::CfgEngine::instance();
    engine.clear_cache();
    const auto sid = sources.add_buffer(name, std::string{src});
    REQUIRE(sid.valid());
    auto parsed_opt = hlsl_clippy::parser::parse(sources, sid);
    REQUIRE(parsed_opt.has_value());
    auto parsed = std::move(parsed_opt.value());
    const ::TSNode root = ::ts_tree_root_node(parsed.tree.get());
    auto cfg_or = engine.build_with_tree(sid, root, parsed.bytes, nullptr, 3U);
    REQUIRE(cfg_or.has_value());
    return Built{std::move(parsed), cfg_or.value(), sid};
}

/// True when `names` contains `target`.
[[nodiscard]] bool contains_name(std::span<const std::string> names, std::string_view target) {
    return std::any_of(names.begin(), names.end(),
                       [&](const std::string& s) { return s == target; });
}

/// Find the `BasicBlockId` whose recorded span tightly encloses `needle`'s
/// first occurrence in `src`. Returns the default-constructed id when no
/// block contains the span (defensive; tests REQUIRE the lookup to hit).
[[nodiscard]] BasicBlockId block_containing(const ControlFlowInfo& cfg,
                                            SourceId sid,
                                            std::string_view src,
                                            std::string_view needle) {
    const auto pos = src.find(needle);
    REQUIRE(pos != std::string_view::npos);
    const ByteSpan target{
        .lo = static_cast<std::uint32_t>(pos),
        .hi = static_cast<std::uint32_t>(pos + needle.size()),
    };
    BasicBlockId best;
    std::uint32_t best_size = 0xFFFFFFFFU;
    // The public CFG API does not expose per-block spans, so we reach
    // through the public impl shared_ptr into the engine-internal
    // CfgStorage. This is test-only -- production rules use higher-
    // level helpers like `rules::util::block_for`.
    if (cfg.cfg.impl != nullptr) {
        const auto& storage = cfg.cfg.impl->data.storage;
        if (storage != nullptr) {
            for (const auto& [block_span, raw] : storage->span_to_block) {
                if (block_span.source != sid) {
                    continue;
                }
                if (block_span.bytes.lo > target.lo || block_span.bytes.hi < target.hi) {
                    continue;
                }
                const auto sz = block_span.bytes.hi - block_span.bytes.lo;
                if (sz < best_size) {
                    best_size = sz;
                    best = BasicBlockId{raw};
                }
            }
        }
    }
    return best;
}

}  // namespace

TEST_CASE("compute_liveness reports x live after `int x = 1; return x;`",
          "[liveness][use-after-def]") {
    SourceManager sources;
    constexpr std::string_view k_src = R"hlsl(
int simple_use_after_def()
{
    int x = 1;
    return x;
}
)hlsl";
    auto built = build(sources, "use_after_def.hlsl", k_src);
    AstTree tree{built.parsed.tree.get(), built.parsed.language, built.parsed.bytes, built.source};

    const auto live = util::compute_liveness(built.cfg, tree);

    // The return block's live_in must contain `x` because the return
    // expression uses it. We locate the block enclosing `return x;`.
    const auto block = block_containing(built.cfg, built.source, k_src, "return x");
    REQUIRE(block.raw() != 0U);
    const auto in = live.live_in_at(block);
    CHECK(contains_name(in, "x"));
}

TEST_CASE("compute_liveness keeps y live at branch merge", "[liveness][branch]") {
    SourceManager sources;
    constexpr std::string_view k_src = R"hlsl(
int branch_merge(int cond)
{
    int y;
    if (cond > 0) {
        y = 1;
    } else {
        y = 2;
    }
    return y;
}
)hlsl";
    auto built = build(sources, "branch_merge.hlsl", k_src);
    AstTree tree{built.parsed.tree.get(), built.parsed.language, built.parsed.bytes, built.source};

    const auto live = util::compute_liveness(built.cfg, tree);

    // The block enclosing `return y;` must have `y` in its live_in --
    // both branches define `y`, so it must be live at the merge point.
    const auto ret_block = block_containing(built.cfg, built.source, k_src, "return y");
    REQUIRE(ret_block.raw() != 0U);
    const auto ret_in = live.live_in_at(ret_block);
    CHECK(contains_name(ret_in, "y"));
}

TEST_CASE("compute_liveness propagates loop-carry locals across the back-edge",
          "[liveness][loop-carry]") {
    SourceManager sources;
    constexpr std::string_view k_src = R"hlsl(
int loop_carry(int n)
{
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        sum = sum + i;
    }
    return sum;
}
)hlsl";
    auto built = build(sources, "loop_carry.hlsl", k_src);
    AstTree tree{built.parsed.tree.get(), built.parsed.language, built.parsed.bytes, built.source};

    const auto live = util::compute_liveness(built.cfg, tree);

    // The CFG builder allocates header / body / exit at identical spans
    // for a `for` statement; locating "the body block" by span alone is
    // ambiguous. Instead we assert that SOME block in this CFG has both
    // `sum` and `i` in its live_in -- that block IS the loop body, and
    // the back-edge propagation is the only mechanism by which `i`
    // (defined intra-loop) lands in any live_in set.
    bool found_loop_body = false;
    for (const auto& entry : live.live_in_per_block) {
        const auto in = live.live_in_at(entry.first);
        if (contains_name(in, "sum") && contains_name(in, "i")) {
            found_loop_body = true;
            break;
        }
    }
    CHECK(found_loop_body);
}

TEST_CASE("compute_liveness keeps locals live across an opaque function call",
          "[liveness][call-boundary]") {
    SourceManager sources;
    constexpr std::string_view k_src = R"hlsl(
float external_call(float a);

float across_call(float input)
{
    float keep = input + 1.0;
    float other = external_call(input);
    return keep + other;
}
)hlsl";
    auto built = build(sources, "across_call.hlsl", k_src);
    AstTree tree{built.parsed.tree.get(), built.parsed.language, built.parsed.bytes, built.source};

    const auto live = util::compute_liveness(built.cfg, tree);

    // The block enclosing `return keep + other` must list both `keep`
    // (defined before the call, used after) AND `other` (defined by the
    // call's assignment, used in the return) in its live_in.
    const auto ret_block = block_containing(built.cfg, built.source, k_src, "return keep + other");
    REQUIRE(ret_block.raw() != 0U);
    const auto ret_in = live.live_in_at(ret_block);
    CHECK(contains_name(ret_in, "keep"));
    CHECK(contains_name(ret_in, "other"));
}

TEST_CASE("compute_liveness reports dead-store local NOT in live_out", "[liveness][dead-store]") {
    SourceManager sources;
    constexpr std::string_view k_src = R"hlsl(
int dead_store(int input)
{
    int unused = input * 2;
    int kept = input + 1;
    return kept;
}
)hlsl";
    auto built = build(sources, "dead_store.hlsl", k_src);
    AstTree tree{built.parsed.tree.get(), built.parsed.language, built.parsed.bytes, built.source};

    const auto live = util::compute_liveness(built.cfg, tree);

    // The entry block defines `unused` but nothing downstream reads it.
    // Locate the entry block by finding the block that encloses the
    // declaration `int unused`. Its live_out must NOT contain `unused`
    // because no downstream block uses it.
    const auto block = block_containing(built.cfg, built.source, k_src, "int unused = input * 2");
    REQUIRE(block.raw() != 0U);
    const auto out = live.live_out_at(block);
    CHECK_FALSE(contains_name(out, "unused"));
    // Sanity: `kept` IS in live_out because the return uses it.
    // (Different block may produce live_out; we test the *return* block's
    // live_in instead, which is a cleaner predicate.)
    const auto ret_block = block_containing(built.cfg, built.source, k_src, "return kept");
    REQUIRE(ret_block.raw() != 0U);
    const auto ret_in = live.live_in_at(ret_block);
    CHECK(contains_name(ret_in, "kept"));
    CHECK_FALSE(contains_name(ret_in, "unused"));
}

TEST_CASE("compute_liveness on empty CFG returns an empty LivenessInfo", "[liveness][empty]") {
    ControlFlowInfo empty_cfg;  // default-constructed; impl handles null
    SourceManager sources;
    constexpr std::string_view k_src = "void empty() {}\n";
    const auto sid = sources.add_buffer("empty.hlsl", std::string{k_src});
    auto parsed_opt = hlsl_clippy::parser::parse(sources, sid);
    REQUIRE(parsed_opt.has_value());
    auto parsed = std::move(parsed_opt.value());
    AstTree tree{parsed.tree.get(), parsed.language, parsed.bytes, parsed.source};

    const auto live = util::compute_liveness(empty_cfg, tree);
    CHECK(live.live_in_per_block.empty());
    CHECK(live.live_out_per_block.empty());
    CHECK(live.live_in_at(BasicBlockId{1U}).empty());
    CHECK(live.live_out_at(BasicBlockId{1U}).empty());
}
