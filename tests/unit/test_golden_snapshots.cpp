// Golden-snapshot regression tests for the rule-pack.
//
// For each fixture under `tests/golden/fixtures/`, run the default rule
// registry via the C++ `lint()` API, marshal the diagnostics to a canonical
// sorted JSON shape, and compare against the on-disk snapshot under
// `tests/golden/snapshots/`. A snapshot diff means a rule's behaviour
// changed; review carefully before refreshing.
//
// Refresh workflow:
//   1. Set env var `SHADER_CLIPPY_GOLDEN_UPDATE=1` and re-run the [golden]
//      tag of this binary; the test will overwrite the snapshot files
//      instead of comparing.
//   2. Or run `tools/update-goldens.ps1` (Windows) / `tools/update-goldens.sh`
//      (Linux/macOS), which wrap this.
//
// On mismatch, the actual output is written next to the expected file with
// the suffix `.actual` so the diff is one `git diff` away.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::Severity;
using shader_clippy::SourceManager;

[[nodiscard]] std::string_view severity_label(Severity sev) noexcept {
    switch (sev) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        case Severity::Note:
            return "note";
    }
    return "warning";
}

[[nodiscard]] std::filesystem::path golden_root() {
    // `k_fixtures_dir` points at `<repo>/tests/fixtures`; the golden root is
    // the sibling `<repo>/tests/golden`.
    std::filesystem::path fixtures{std::string{shader_clippy::test::k_fixtures_dir}};
    return fixtures.parent_path() / "golden";
}

[[nodiscard]] bool update_mode() noexcept {
    // Use `getenv` (POSIX/MSVC both support it). Treat any non-empty value
    // other than "0" as "update".
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- tests are single-threaded.
    const char* raw = std::getenv("SHADER_CLIPPY_GOLDEN_UPDATE");
    if (raw == nullptr) {
        return false;
    }
    const std::string_view value{raw};
    return !value.empty() && value != "0";
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

/// Strip every `\r` so CRLF and LF inputs hash to the same byte sequence.
/// Required because git on Windows may check out the snapshot files with
/// CRLF (depending on `core.autocrlf` and `.gitattributes` history) while
/// the in-memory `actual` is generated with LF.
[[nodiscard]] std::string normalize_line_endings(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c != '\r') {
            out.push_back(c);
        }
    }
    return out;
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

/// Build the canonical JSON shape for one fixture's diagnostics.
///
/// Shape:
///   { "fixture": "<basename>", "diagnostics": [ {rule, line, col, severity,
///   message}, ... ] }
///
/// Sort key: (line, col, rule). Stable across runs and platforms.
[[nodiscard]] nlohmann::ordered_json marshal(const std::string& fixture_name,
                                             const SourceManager& sources,
                                             const std::vector<Diagnostic>& diagnostics) {
    struct Row {
        std::string rule;
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        std::string severity;
        std::string message;
    };

    std::vector<Row> rows;
    rows.reserve(diagnostics.size());
    for (const auto& d : diagnostics) {
        // Filter out engine-infrastructure diagnostics (`clippy::reflection`,
        // `clippy::cfg`, `clippy::cfg-skip`, `clippy::malformed-suppression`).
        // Their messages contain absolute filesystem paths that vary per
        // machine, and they tell us about Slang/CFG plumbing rather than
        // about a rule's behaviour. The snapshot is a rule-pack regression
        // gate; infrastructure noise belongs in a separate harness.
        if (d.code.starts_with("clippy::")) {
            continue;
        }
        const auto loc = sources.resolve(d.primary_span.source, d.primary_span.bytes.lo);
        rows.push_back(Row{
            .rule = d.code,
            .line = loc.line,
            .column = loc.column,
            .severity = std::string{severity_label(d.severity)},
            .message = d.message,
        });
    }
    // Sort key: (line, col, rule, message). The four-key sort guarantees a
    // stable order even when two diagnostics share a line/col/rule (e.g.
    // `cbuffer-fits-rootconstants` firing on two cbuffers both spanning
    // 1:1). Without the message tiebreaker the order was implementation-
    // defined and snapshots flapped on adjacent commits.
    std::ranges::sort(rows, [](const Row& a, const Row& b) {
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.column != b.column) {
            return a.column < b.column;
        }
        if (a.rule != b.rule) {
            return a.rule < b.rule;
        }
        return a.message < b.message;
    });

    nlohmann::ordered_json out;
    out["fixture"] = fixture_name;
    auto array = nlohmann::ordered_json::array();
    for (const auto& r : rows) {
        nlohmann::ordered_json entry;
        entry["rule"] = r.rule;
        entry["line"] = r.line;
        entry["col"] = r.column;
        entry["severity"] = r.severity;
        entry["message"] = r.message;
        array.push_back(std::move(entry));
    }
    out["diagnostics"] = std::move(array);
    return out;
}

/// Pretty-print with 2-space indent and trailing newline so the snapshot file
/// is git-friendly. Stable byte-for-byte across nlohmann releases that keep
/// the dump format.
[[nodiscard]] std::string canonical_dump(const nlohmann::ordered_json& j) {
    std::string s = j.dump(2);
    s.push_back('\n');
    return s;
}

void run_one_fixture(const std::filesystem::path& fixture_path,
                     const std::filesystem::path& snapshot_path) {
    REQUIRE(std::filesystem::exists(fixture_path));

    SourceManager sources;
    const auto src = sources.add_file(fixture_path);
    REQUIRE(src.valid());

    auto rules = make_default_rules();
    const auto diagnostics = lint(sources, src, rules);

    const std::string fixture_name = fixture_path.filename().string();
    const auto j = marshal(fixture_name, sources, diagnostics);
    const std::string actual = canonical_dump(j);

    if (update_mode()) {
        std::filesystem::create_directories(snapshot_path.parent_path());
        write_file(snapshot_path, actual);
        SUCCEED("snapshot updated: " << snapshot_path.string());
        return;
    }

    if (!std::filesystem::exists(snapshot_path)) {
        // First-run convenience: persist the .actual sibling so the user can
        // inspect and `mv` it into place, but fail loudly so CI doesn't
        // silently accept missing snapshots.
        const auto actual_path = snapshot_path.string() + ".actual";
        std::filesystem::create_directories(snapshot_path.parent_path());
        write_file(actual_path, actual);
        FAIL("missing snapshot: "
             << snapshot_path.string() << "\nWrote actual output to: " << actual_path
             << "\nRun tools/update-goldens.{ps1,sh} or set SHADER_CLIPPY_GOLDEN_UPDATE=1.");
        return;
    }

    const std::string expected = read_file(snapshot_path);
    if (normalize_line_endings(expected) != normalize_line_endings(actual)) {
        const auto actual_path = snapshot_path.string() + ".actual";
        write_file(actual_path, actual);
        FAIL("snapshot mismatch for "
             << fixture_name << "\nExpected: " << snapshot_path.string()
             << "\nActual:   " << actual_path
             << "\nRun tools/update-goldens.{ps1,sh} to refresh, then review the diff "
                "before committing.");
    }
}

}  // namespace

TEST_CASE("golden snapshot phase2-math", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase2-math.hlsl",
                    root / "snapshots" / "phase2-math.json");
}

TEST_CASE("golden snapshot phase2-redundancy", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase2-redundancy.hlsl",
                    root / "snapshots" / "phase2-redundancy.json");
}

TEST_CASE("golden snapshot phase2-misc", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase2-misc.hlsl",
                    root / "snapshots" / "phase2-misc.json");
}

TEST_CASE("golden snapshot phase3-bindings", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase3-bindings.hlsl",
                    root / "snapshots" / "phase3-bindings.json");
}

TEST_CASE("golden snapshot phase3-texture", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase3-texture.hlsl",
                    root / "snapshots" / "phase3-texture.json");
}

TEST_CASE("golden snapshot phase3-workgroup", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase3-workgroup.hlsl",
                    root / "snapshots" / "phase3-workgroup.json");
}

TEST_CASE("golden snapshot phase4-control-flow", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase4-control-flow.hlsl",
                    root / "snapshots" / "phase4-control-flow.json");
}

TEST_CASE("golden snapshot phase4-atomics", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase4-atomics.hlsl",
                    root / "snapshots" / "phase4-atomics.json");
}

TEST_CASE("golden snapshot phase4-numerical-safety", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase4-numerical-safety.hlsl",
                    root / "snapshots" / "phase4-numerical-safety.json");
}

TEST_CASE("golden snapshot phase7-raytracing", "[golden]") {
    const auto root = golden_root();
    run_one_fixture(root / "fixtures" / "phase7-raytracing.hlsl",
                    root / "snapshots" / "phase7-raytracing.json");
}
