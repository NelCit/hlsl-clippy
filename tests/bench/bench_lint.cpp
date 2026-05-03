// Performance benchmark for `shader_clippy::lint()` over the public 27-shader
// corpus.
//
// What this benchmark measures
// ----------------------------
// For each fixture under `tests/corpus/<stage>/*.hlsl`, we BENCHMARK a single
// end-to-end lint pass:
//
//     1. Construct a fresh `SourceManager`.
//     2. Register the file via `add_file()`.
//     3. Build the default rule pack (`make_default_rules()` -> all rules).
//     4. Invoke `lint(sources, src, rules)`.
//     5. Discard the diagnostics.
//
// Catch2's chronometer does the iteration accounting (default 100 samples)
// and reports median + stddev. Because each iteration spins up its own
// `SourceManager`, file IO and parser-state caches are NOT amortised -- this
// is intentional: it mirrors the hot path the CLI walks for every file and
// keeps the per-iteration cost honest. If we ever want a "warm" version
// (parse once, lint many) we can add a second BENCHMARK below; the headline
// CI gate is the cold per-file number.
//
// We also expose an aggregate "lint full corpus" benchmark that walks every
// file in one iteration body. Its median is the rollup CI watches; per-file
// numbers point at the offending fixture when the rollup regresses.
//
// Build / run
// -----------
// See `tests/bench/README.md`. Bench builds in Release / RelWithDebInfo only;
// in Debug the target is skipped at CMake-time so `cmake --build` against a
// debug tree doesn't try to compile this TU under -O0 (where any number it
// produces is meaningless).

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "bench_config.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

/// Relative paths (POSIX-style, joined under tests/corpus/) of every fixture
/// the bench drives. Hard-coded to keep the BENCHMARK macro list flat: each
/// entry expands to one named benchmark, which is what Catch2 prints in its
/// table. Update this list whenever the corpus changes -- `tests/corpus/`
/// has 27 entries today.
constexpr std::array<std::string_view, 27> k_corpus_files{
    // amplification (2)
    "amplification/dynamic_lod_as.hlsl",
    "amplification/meshlet_cull_as.hlsl",
    // compute (11)
    "compute/bitonic_presort_cs.hlsl",
    "compute/bloom_downsample_cs.hlsl",
    "compute/fill_light_grid_cs.hlsl",
    "compute/gaussian_blur_cs.hlsl",
    "compute/generate_histogram_cs.hlsl",
    "compute/hello_work_graph_wg.hlsl",
    "compute/indirect_cull_cs.hlsl",
    "compute/nbody_gravity_cs.hlsl",
    "compute/particle_update_cs.hlsl",
    "compute/spd_cs_downsampler.hlsl",
    "compute/tone_map_cs.hlsl",
    // mesh (1)
    "mesh/meshlet_render_ms.hlsl",
    // pixel (4)
    "pixel/meshlet_blinn_phong_ps.hlsl",
    "pixel/model_viewer_ps.hlsl",
    "pixel/skybox_ibl_ps.hlsl",
    "pixel/wave_intrinsics_ps.hlsl",
    // raytracing (5)
    "raytracing/closest_hit_shader.hlsl",
    "raytracing/miss_shader.hlsl",
    "raytracing/ray_generation.hlsl",
    "raytracing/rt_procedural_geometry.hlsl",
    "raytracing/rt_simple_lighting.hlsl",
    // vertex (4)
    "vertex/depth_prepass_vs.hlsl",
    "vertex/model_viewer_vs.hlsl",
    "vertex/particle_instanced_vs.hlsl",
    "vertex/skinned_mesh_vs.hlsl",
};

[[nodiscard]] std::filesystem::path corpus_path(std::string_view rel) {
    std::filesystem::path p{std::string{shader_clippy::bench::k_corpus_dir}};
    p /= std::string{rel};
    return p;
}

/// Run one lint pass against `path` from a fresh SourceManager + fresh rule
/// pack. Returned vector is consumed by the BENCHMARK harness so the
/// optimiser can't elide the call.
[[nodiscard]] std::vector<Diagnostic> lint_one(const std::filesystem::path& path) {
    SourceManager sources;
    const auto src = sources.add_file(path);
    if (!src.valid()) {
        return {};
    }
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

/// Extract the basename without extension so BENCHMARK names stay stable
/// regardless of the directory layout. ASCII-only.
[[nodiscard]] std::string benchmark_name(std::string_view rel) {
    const auto slash = rel.find_last_of('/');
    const auto base = (slash == std::string_view::npos) ? rel : rel.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    const auto stem = (dot == std::string_view::npos) ? base : base.substr(0, dot);
    return std::string{"lint "} + std::string{stem};
}

}  // namespace

TEST_CASE("shader-clippy lint perf - per-file corpus walk", "[!benchmark][bench][perf]") {
    // Sanity-check the corpus layout up front so a missing file produces a
    // clean test failure instead of every BENCHMARK silently reporting
    // "lint NULL" timings.
    for (const auto rel : k_corpus_files) {
        const auto p = corpus_path(rel);
        REQUIRE(std::filesystem::exists(p));
    }

    // One BENCHMARK per fixture. The lambda captures the path by value so
    // each iteration body is a self-contained closure; the SourceManager is
    // constructed inside the lambda, not in setup, so cache pollution from
    // a long-lived manager doesn't skew the timing.
    //
    // Catch2's BENCHMARK(name) takes `std::string&&`, so we feed it the
    // freshly-built name as an rvalue via std::move to dodge the lvalue
    // overload-resolution failure.
    for (const auto rel : k_corpus_files) {
        const auto p = corpus_path(rel);
        auto name = benchmark_name(rel);
        BENCHMARK(std::move(name)) {
            return lint_one(p);
        };
    }
}

TEST_CASE("shader-clippy lint perf - full corpus rollup", "[!benchmark][bench][perf]") {
    // Verify every fixture exists before timing -- see the per-file test
    // above for the rationale.
    std::vector<std::filesystem::path> paths;
    paths.reserve(k_corpus_files.size());
    for (const auto rel : k_corpus_files) {
        auto p = corpus_path(rel);
        REQUIRE(std::filesystem::exists(p));
        paths.push_back(std::move(p));
    }

    BENCHMARK("lint full corpus") {
        std::size_t total_diagnostics = 0;
        for (const auto& p : paths) {
            const auto diags = lint_one(p);
            total_diagnostics += diags.size();
        }
        return total_diagnostics;
    };
}
