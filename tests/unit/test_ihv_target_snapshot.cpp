// IHV-target snapshot test (ADR 0018 §5 criterion #11).
//
// Loads `tests/fixtures/phase8/experimental_targets.hlsl`, runs `lint()` once
// for each `ExperimentalTarget` value, and asserts the rule-firing shape:
//
//   * Default config (`ExperimentalTarget::None`): zero diagnostics with a
//     code containing `-on-rdna4` / `-blackwell-` / `-on-xe2`. This is the
//     in-tree complement of the CI `ihv-target-snapshot` job that snapshots
//     the JSON output: by enforcing the *no-IHV-noise-by-default* invariant
//     in the unit suite, regressions surface locally during development
//     instead of only at CI time.
//   * `Rdna4`: at least one diagnostic with a code containing `rdna4`.
//   * `Blackwell`: at least one diagnostic with a code containing
//     `blackwell`.
//   * `Xe2`: at least one diagnostic with a code containing `xe2`.
//
// Every rule that opts into an experimental target via
// `Rule::experimental_target()` is silently skipped under any other target,
// so the four runs together exercise the whole IHV-experimental dispatch
// surface in one fixture.
//
// The fixture is the same file the CI snapshot job pins
// (`tests/fixtures/phase8/experimental_targets.hlsl`) so the in-tree test
// and the JSON snapshots cannot drift apart.

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using hlsl_clippy::Config;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::ExperimentalTarget;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::filesystem::path fixture_path() {
    std::filesystem::path fixtures{std::string{hlsl_clippy::test::k_fixtures_dir}};
    return fixtures / "phase8" / "experimental_targets.hlsl";
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return std::string{std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<Diagnostic> lint_under(ExperimentalTarget target) {
    const auto path = fixture_path();
    const auto source_text = read_file(path);

    SourceManager sources;
    const auto src = sources.add_buffer(path.string(), source_text);
    REQUIRE(src.valid());

    auto rules = make_default_rules();

    Config cfg{};
    cfg.experimental_target_value = target;

    LintOptions opts;
    // The fixture mixes compute, mesh, raygen and closesthit entry points
    // in one TU. We let Slang pick its default per-stage profile by
    // leaving `target_profile` empty -- the bridge falls back to its
    // canonical `sm_6_6` and Slang's lib-style ingestion enumerates each
    // `[shader("...")]`-tagged entry point at its declared stage.
    opts.target_profile = std::nullopt;

    return lint(sources, src, rules, cfg, path, opts);
}

[[nodiscard]] bool any_code_contains(const std::vector<Diagnostic>& diags,
                                     std::string_view needle) {
    for (const auto& d : diags) {
        if (d.code.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t count_ihv_codes(const std::vector<Diagnostic>& diags) {
    // Engine-internal `clippy::*` diagnostics are not IHV-target rules and
    // must not be counted here — they fire for reflection / CFG plumbing
    // reasons that are independent of the active experimental target.
    static constexpr std::array<std::string_view, 3> k_needles{{
        "-on-rdna4",
        "-blackwell-",
        "-on-xe2",
    }};
    std::size_t count = 0;
    for (const auto& d : diags) {
        if (d.code.starts_with("clippy::")) {
            continue;
        }
        for (const auto needle : k_needles) {
            if (d.code.find(needle) != std::string::npos) {
                ++count;
                break;
            }
        }
    }
    return count;
}

}  // namespace

TEST_CASE("Default config emits zero IHV-target-gated diagnostics on the experimental fixture",
          "[ihv-target][snapshot][experimental]") {
    const auto diags = lint_under(ExperimentalTarget::None);
    // Criterion #11 of ADR 0018 §5: default `.hlsl-clippy.toml` produces
    // zero IHV-specific diagnostics. We allow other rules to fire (the
    // fixture is real HLSL and trips e.g. `numthreads-too-small` /
    // `dispatchmesh-grid-too-small-for-wave`) but no rule whose id encodes
    // an IHV target may surface.
    CHECK(count_ihv_codes(diags) == 0U);
}

TEST_CASE("Rdna4 config surfaces at least one rdna4-coded diagnostic on the experimental fixture",
          "[ihv-target][snapshot][experimental]") {
    const auto diags = lint_under(ExperimentalTarget::Rdna4);
    CHECK(any_code_contains(diags, "rdna4"));
}

TEST_CASE("Blackwell config surfaces at least one blackwell-coded diagnostic on the experimental fixture",
          "[ihv-target][snapshot][experimental]") {
    const auto diags = lint_under(ExperimentalTarget::Blackwell);
    CHECK(any_code_contains(diags, "blackwell"));
}

TEST_CASE("Xe2 config surfaces at least one xe2-coded diagnostic on the experimental fixture",
          "[ihv-target][snapshot][experimental]") {
    const auto diags = lint_under(ExperimentalTarget::Xe2);
    CHECK(any_code_contains(diags, "xe2"));
}
