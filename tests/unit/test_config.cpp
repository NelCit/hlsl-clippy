// Unit tests for the .hlsl-clippy.toml config loader.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/config.hpp"

namespace {

/// RAII unique-temp-directory helper. Each test gets its own subdir under the
/// system temp root; the directory tree is recursively removed in the
/// destructor so successive runs don't accumulate state.
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        std::mt19937_64 rng{rd()};
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto unique = stamp ^ rng();

        char buf[64];  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        // Hex format keeps the directory name short and OS-portable.
        const auto written = std::snprintf(  // NOLINT(cert-err33-c)
            static_cast<char*>(buf),
            sizeof(buf),
            "hlsl-clippy-test-%016llx",
            static_cast<unsigned long long>(unique));
        REQUIRE(written > 0);
        path_ = std::filesystem::temp_directory_path() / buf;
        std::filesystem::create_directories(path_);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    void write(const std::filesystem::path& relative, std::string_view contents) const {
        const auto absolute = path_ / relative;
        std::filesystem::create_directories(absolute.parent_path());
        std::ofstream out(absolute, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    void mkdir(const std::filesystem::path& relative) const {
        std::filesystem::create_directories(path_ / relative);
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Empty config parses to a default Config", "[config]") {
    const auto result = hlsl_clippy::load_config_string("");
    REQUIRE(result.has_value());
    const auto& cfg = result.value();
    CHECK(cfg.rule_severity.empty());
    CHECK(cfg.includes.empty());
    CHECK(cfg.excludes.empty());
    CHECK(cfg.overrides.empty());
}

TEST_CASE("[rules] table sets per-rule severity", "[config]") {
    constexpr std::string_view k_toml = R"(
[rules]
pow-const-squared = "deny"
redundant-saturate = "warn"
clamp01-to-saturate = "allow"
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE(result.has_value());
    const auto& cfg = result.value();
    REQUIRE(cfg.rule_severity.size() == 3U);

    const auto sev_pow = cfg.severity_for("pow-const-squared", "shaders/foo.hlsl");
    REQUIRE(sev_pow.has_value());
    CHECK(*sev_pow == hlsl_clippy::RuleSeverity::Deny);

    const auto sev_red = cfg.severity_for("redundant-saturate", "shaders/foo.hlsl");
    REQUIRE(sev_red.has_value());
    CHECK(*sev_red == hlsl_clippy::RuleSeverity::Warn);

    const auto sev_clamp = cfg.severity_for("clamp01-to-saturate", "shaders/foo.hlsl");
    REQUIRE(sev_clamp.has_value());
    CHECK(*sev_clamp == hlsl_clippy::RuleSeverity::Allow);
}

TEST_CASE("[[overrides]] win over [rules] when the path glob matches", "[config][overrides]") {
    constexpr std::string_view k_toml = R"(
[rules]
redundant-saturate = "deny"

[[overrides]]
path = "shaders/legacy/**"
rules = { redundant-saturate = "allow" }
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE(result.has_value());
    const auto& cfg = result.value();

    // Inside the legacy directory: override wins.
    const auto inside = cfg.severity_for("redundant-saturate", "shaders/legacy/foo.hlsl");
    REQUIRE(inside.has_value());
    CHECK(*inside == hlsl_clippy::RuleSeverity::Allow);

    // Outside: base [rules] applies.
    const auto outside = cfg.severity_for("redundant-saturate", "shaders/main.hlsl");
    REQUIRE(outside.has_value());
    CHECK(*outside == hlsl_clippy::RuleSeverity::Deny);
}

TEST_CASE("Path glob matcher handles ** and *", "[config][glob]") {
    using hlsl_clippy::path_glob_match;

    CHECK(path_glob_match("**/*.hlsl", "shaders/foo.hlsl"));
    CHECK(path_glob_match("**/*.hlsl", "shaders/sub/foo.hlsl"));
    CHECK(path_glob_match("**/*.hlsl", "foo.hlsl"));
    CHECK_FALSE(path_glob_match("**/*.hlsl", "foo.txt"));
    CHECK(path_glob_match("shaders/legacy/**", "shaders/legacy/foo.hlsl"));
    CHECK(path_glob_match("shaders/legacy/**", "shaders/legacy/sub/foo.hlsl"));
    CHECK_FALSE(path_glob_match("shaders/legacy/**", "shaders/main.hlsl"));
    CHECK(path_glob_match("shaders/*.hlsl", "shaders/foo.hlsl"));
    CHECK_FALSE(path_glob_match("shaders/*.hlsl", "shaders/sub/foo.hlsl"));
}

TEST_CASE("find_config walks parent directories", "[config][resolver]") {
    TempDir tmp;
    // Lay out: tmp/.git/  +  tmp/.hlsl-clippy.toml  +  tmp/a/b/c/file.hlsl
    tmp.mkdir(".git");
    tmp.write(".hlsl-clippy.toml", "");
    tmp.write("a/b/c/file.hlsl", "// shader");

    const auto target = tmp.path() / "a" / "b" / "c" / "file.hlsl";
    const auto found = hlsl_clippy::find_config(target);
    REQUIRE(found.has_value());
    CHECK(std::filesystem::equivalent(*found, tmp.path() / ".hlsl-clippy.toml"));
}

TEST_CASE("find_config stops at .git workspace boundary", "[config][resolver]") {
    TempDir tmp;
    // Layout:
    //   tmp/outer/.hlsl-clippy.toml      <- should NOT be picked
    //   tmp/outer/inner/.git
    //   tmp/outer/inner/file.hlsl
    tmp.write("outer/.hlsl-clippy.toml", "");
    tmp.mkdir("outer/inner/.git");
    tmp.write("outer/inner/file.hlsl", "// shader");

    const auto target = tmp.path() / "outer" / "inner" / "file.hlsl";
    const auto found = hlsl_clippy::find_config(target);
    CHECK_FALSE(found.has_value());
}

TEST_CASE("Malformed TOML returns ConfigError with line + column", "[config][error]") {
    constexpr std::string_view k_bad = R"(
[rules
pow-const-squared = "warn"
)";
    const auto result = hlsl_clippy::load_config_string(k_bad, "synthetic.toml");
    REQUIRE_FALSE(result.has_value());
    const auto& err = result.error();
    CHECK(err.source == std::filesystem::path{"synthetic.toml"});
    CHECK(err.line > 0U);
    CHECK_FALSE(err.message.empty());
}

TEST_CASE("Invalid severity string is rejected", "[config][error]") {
    constexpr std::string_view k_toml = R"(
[rules]
pow-const-squared = "panic"
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("panic") != std::string::npos);
}

TEST_CASE("includes/excludes patterns parse into vectors", "[config]") {
    constexpr std::string_view k_toml = R"(
[includes]
patterns = ["**/*.hlsl", "**/*.hlsli"]

[excludes]
patterns = ["external/**"]
)";
    const auto result = hlsl_clippy::load_config_string(k_toml);
    REQUIRE(result.has_value());
    const auto& cfg = result.value();
    REQUIRE(cfg.includes.size() == 2U);
    CHECK(cfg.includes[0] == "**/*.hlsl");
    CHECK(cfg.includes[1] == "**/*.hlsli");
    REQUIRE(cfg.excludes.size() == 1U);
    CHECK(cfg.excludes[0] == "external/**");
}
