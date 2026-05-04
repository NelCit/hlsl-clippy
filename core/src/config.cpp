// `.shader-clippy.toml` loader (per ADR 0008 §5).
//
// Schema:
//   [rules]
//   pow-const-squared = "warn"
//   redundant-saturate = "deny"
//   ...
//
//   [includes]
//   patterns = ["**/*.hlsl"]
//
//   [excludes]
//   patterns = ["external/**"]
//
//   [[overrides]]
//   path = "shaders/legacy/**"
//   rules = { redundant-saturate = "allow" }
//
// The file is parsed via toml++ in single-header mode. Every diagnostic
// surface has a `clippy::config` diagnostic equivalent emitted by the driver.

#include "shader_clippy/config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

namespace shader_clippy {

namespace {

[[nodiscard]] std::optional<RuleSeverity> parse_rule_severity(std::string_view text) noexcept {
    if (text == "allow") {
        return RuleSeverity::Allow;
    }
    if (text == "warn") {
        return RuleSeverity::Warn;
    }
    if (text == "deny") {
        return RuleSeverity::Deny;
    }
    return std::nullopt;
}

/// Parse `[lint] source-language` value. Returns `SourceLanguage::Auto`
/// for empty / unrecognised input. The unrecognised case is reported via
/// the second tuple element (`true` -> `warnings.push_back` is in order).
[[nodiscard]] std::pair<SourceLanguage, bool> parse_source_language(
    std::string_view text) noexcept {
    if (text.empty() || text == "auto") {
        return {SourceLanguage::Auto, false};
    }
    if (text == "hlsl") {
        return {SourceLanguage::Hlsl, false};
    }
    if (text == "slang") {
        return {SourceLanguage::Slang, false};
    }
    return {SourceLanguage::Auto, true};
}

/// Parse `[experimental] target` value. Returns `ExperimentalTarget::None`
/// for empty / unrecognised input. The unrecognised case is reported back
/// via the second tuple element (`true` -> `warnings.push_back` is in order).
[[nodiscard]] std::pair<ExperimentalTarget, bool> parse_experimental_target(
    std::string_view text) noexcept {
    if (text.empty()) {
        return {ExperimentalTarget::None, false};
    }
    if (text == "rdna4") {
        return {ExperimentalTarget::Rdna4, false};
    }
    if (text == "blackwell") {
        return {ExperimentalTarget::Blackwell, false};
    }
    if (text == "xe2") {
        return {ExperimentalTarget::Xe2, false};
    }
    return {ExperimentalTarget::None, true};
}

[[nodiscard]] ConfigError make_error(std::string message,
                                     const std::filesystem::path& source,
                                     std::uint32_t line = 0,
                                     std::uint32_t column = 0) {
    ConfigError err;
    err.message = std::move(message);
    err.source = source;
    err.line = line;
    err.column = column;
    return err;
}

[[nodiscard]] ConfigError from_parse_error(const ::toml::parse_error& parse_err,
                                           const std::filesystem::path& source) {
    const auto& src = parse_err.source();
    ConfigError err;
    err.message = std::string{parse_err.description()};
    err.source = source;
    err.line = static_cast<std::uint32_t>(src.begin.line);
    err.column = static_cast<std::uint32_t>(src.begin.column);
    return err;
}

[[nodiscard]] std::filesystem::path resolve_config_relative_path(
    std::string_view text, const std::filesystem::path& source) {
    std::filesystem::path path{std::string{text}};
    if (path.is_absolute() || source.empty()) {
        return path.lexically_normal();
    }
    const auto base = source.has_parent_path() ? source.parent_path() : std::filesystem::path{};
    if (base.empty()) {
        return path.lexically_normal();
    }
    return (base / path).lexically_normal();
}

[[nodiscard]] ConfigResult parse_root(const ::toml::table& root,
                                      const std::filesystem::path& source) {
    Config cfg;

    // [rules]
    if (const auto* rules_node = root.get("rules"); rules_node != nullptr) {
        const auto* rules_tbl = rules_node->as_table();
        if (rules_tbl == nullptr) {
            return make_error("`rules` must be a table", source);
        }
        for (const auto& [key, value] : *rules_tbl) {
            const auto* str = value.as_string();
            if (str == nullptr) {
                return make_error("`rules." + std::string{key.str()} +
                                      "` must be a string (\"allow\", \"warn\", or \"deny\")",
                                  source,
                                  static_cast<std::uint32_t>(value.source().begin.line),
                                  static_cast<std::uint32_t>(value.source().begin.column));
            }
            const auto sev = parse_rule_severity(str->get());
            if (!sev.has_value()) {
                return make_error("invalid severity \"" + std::string{str->get()} +
                                      "\" for rule `" + std::string{key.str()} +
                                      "`: expected \"allow\", \"warn\", or \"deny\"",
                                  source,
                                  static_cast<std::uint32_t>(value.source().begin.line),
                                  static_cast<std::uint32_t>(value.source().begin.column));
            }
            cfg.rule_severity.emplace(std::string{key.str()}, *sev);
        }
    }

    // [includes]
    if (const auto* inc = root.get("includes"); inc != nullptr) {
        const auto* tbl = inc->as_table();
        if (tbl == nullptr) {
            return make_error("`includes` must be a table", source);
        }
        if (const auto* pat = tbl->get("patterns"); pat != nullptr) {
            const auto* arr = pat->as_array();
            if (arr == nullptr) {
                return make_error("`includes.patterns` must be an array of strings", source);
            }
            for (const auto& el : *arr) {
                const auto* s = el.as_string();
                if (s == nullptr) {
                    return make_error("`includes.patterns` entries must be strings", source);
                }
                cfg.includes.push_back(s->get());
            }
        }
    }

    // [excludes]
    if (const auto* exc = root.get("excludes"); exc != nullptr) {
        const auto* tbl = exc->as_table();
        if (tbl == nullptr) {
            return make_error("`excludes` must be a table", source);
        }
        if (const auto* pat = tbl->get("patterns"); pat != nullptr) {
            const auto* arr = pat->as_array();
            if (arr == nullptr) {
                return make_error("`excludes.patterns` must be an array of strings", source);
            }
            for (const auto& el : *arr) {
                const auto* s = el.as_string();
                if (s == nullptr) {
                    return make_error("`excludes.patterns` entries must be strings", source);
                }
                cfg.excludes.push_back(s->get());
            }
        }
    }

    // [shader] include-directories
    if (const auto* shader = root.get("shader"); shader != nullptr) {
        const auto* tbl = shader->as_table();
        if (tbl == nullptr) {
            return make_error("`shader` must be a table", source);
        }
        if (const auto* dirs = tbl->get("include-directories"); dirs != nullptr) {
            const auto* arr = dirs->as_array();
            if (arr == nullptr) {
                return make_error("`shader.include-directories` must be an array of strings",
                                  source);
            }
            for (const auto& el : *arr) {
                const auto* s = el.as_string();
                if (s == nullptr) {
                    return make_error("`shader.include-directories` entries must be strings",
                                      source);
                }
                cfg.shader_include_directories.push_back(
                    resolve_config_relative_path(s->get(), source));
            }
        }
    }

    // [float] compare-epsilon / div-epsilon (v1.2, ADR 0019).
    //
    // toml++ keeps integers and floats in distinct value types; users may
    // legitimately write `compare-epsilon = 1` (integer) or
    // `compare-epsilon = 0.05` (float). We accept both and reject anything
    // else (string, array, table, bool) with a soft warning + default
    // fallback so an old config never breaks the lint pipeline.
    if (const auto* fl = root.get("float"); fl != nullptr) {
        const auto* tbl = fl->as_table();
        if (tbl == nullptr) {
            return make_error("`float` must be a table", source);
        }

        auto parse_epsilon = [&](std::string_view key, float& out_value, float default_value) {
            const auto* node = tbl->get(key);
            if (node == nullptr) {
                return;
            }
            std::optional<double> parsed;
            if (const auto* f = node->as_floating_point(); f != nullptr) {
                parsed = f->get();
            } else if (const auto* i = node->as_integer(); i != nullptr) {
                parsed = static_cast<double>(i->get());
            }
            if (!parsed.has_value() || !(*parsed > 0.0) || !(*parsed < 1.0)) {
                std::string msg = "`float.";
                msg += key;
                msg +=
                    "` must be a positive numeric value strictly less than 1.0; "
                    "falling back to the default";
                cfg.warnings.push_back(std::move(msg));
                out_value = default_value;
                return;
            }
            out_value = static_cast<float>(*parsed);
        };

        parse_epsilon("compare-epsilon", cfg.compare_epsilon_value, k_default_compare_epsilon);
        parse_epsilon("div-epsilon", cfg.div_epsilon_value, k_default_div_epsilon);
    }

    // [lint] source-language (v1.3, ADR 0020 sub-phase A).
    //
    // Selects which frontend the orchestrator engages for files matched by
    // this config. Default `auto` -> per-file extension inference. Explicit
    // `hlsl` / `slang` overrides the per-file inference. Unknown values fall
    // back to `auto` with a soft warning so an old config never breaks the
    // lint pipeline.
    if (const auto* lint = root.get("lint"); lint != nullptr) {
        const auto* tbl = lint->as_table();
        if (tbl == nullptr) {
            return make_error("`lint` must be a table", source);
        }
        if (const auto* sl = tbl->get("source-language"); sl != nullptr) {
            const auto* str = sl->as_string();
            if (str == nullptr) {
                return make_error(
                    "`lint.source-language` must be a string "
                    "(\"auto\", \"hlsl\", or \"slang\")",
                    source,
                    static_cast<std::uint32_t>(sl->source().begin.line),
                    static_cast<std::uint32_t>(sl->source().begin.column));
            }
            const auto [parsed, unknown] = parse_source_language(str->get());
            cfg.source_language_value = parsed;
            if (unknown) {
                std::string msg = "unrecognised `lint.source-language = \"";
                msg += str->get();
                msg += "\"`: expected \"auto\", \"hlsl\", or \"slang\"; falling back to \"auto\"";
                cfg.warnings.push_back(std::move(msg));
            }
        }
    }

    // [experimental] target
    if (const auto* exp = root.get("experimental"); exp != nullptr) {
        const auto* tbl = exp->as_table();
        if (tbl == nullptr) {
            return make_error("`experimental` must be a table", source);
        }
        if (const auto* tgt = tbl->get("target"); tgt != nullptr) {
            const auto* str = tgt->as_string();
            if (str == nullptr) {
                return make_error(
                    "`experimental.target` must be a string "
                    "(\"rdna4\", \"blackwell\", \"xe2\", or empty)",
                    source,
                    static_cast<std::uint32_t>(tgt->source().begin.line),
                    static_cast<std::uint32_t>(tgt->source().begin.column));
            }
            const auto [parsed, unknown] = parse_experimental_target(str->get());
            cfg.experimental_target_value = parsed;
            if (unknown) {
                std::string msg = "unrecognised `experimental.target = \"";
                msg += str->get();
                msg +=
                    "\"`: expected \"rdna4\", \"blackwell\", \"xe2\", or empty; "
                    "falling back to no experimental target";
                cfg.warnings.push_back(std::move(msg));
            }
        }
    }

    // [[overrides]]
    if (const auto* ov = root.get("overrides"); ov != nullptr) {
        const auto* arr = ov->as_array();
        if (arr == nullptr) {
            return make_error("`overrides` must be an array of tables", source);
        }
        for (const auto& el : *arr) {
            const auto* tbl = el.as_table();
            if (tbl == nullptr) {
                return make_error("`overrides` entries must be tables", source);
            }
            RuleOverride entry;
            if (const auto* p = tbl->get("path"); p != nullptr) {
                const auto* s = p->as_string();
                if (s == nullptr) {
                    return make_error("`overrides[].path` must be a string", source);
                }
                entry.path_glob = s->get();
            } else {
                return make_error("`overrides[]` requires a `path` field", source);
            }
            if (const auto* r = tbl->get("rules"); r != nullptr) {
                const auto* rt = r->as_table();
                if (rt == nullptr) {
                    return make_error("`overrides[].rules` must be a table", source);
                }
                for (const auto& [key, value] : *rt) {
                    const auto* sv = value.as_string();
                    if (sv == nullptr) {
                        return make_error(
                            "`overrides[].rules." + std::string{key.str()} + "` must be a string",
                            source,
                            static_cast<std::uint32_t>(value.source().begin.line),
                            static_cast<std::uint32_t>(value.source().begin.column));
                    }
                    const auto sev = parse_rule_severity(sv->get());
                    if (!sev.has_value()) {
                        return make_error("invalid severity for `overrides[].rules." +
                                              std::string{key.str()} + "`",
                                          source,
                                          static_cast<std::uint32_t>(value.source().begin.line),
                                          static_cast<std::uint32_t>(value.source().begin.column));
                    }
                    entry.rule_severity.emplace(std::string{key.str()}, *sev);
                }
            }
            cfg.overrides.push_back(std::move(entry));
        }
    }

    return ConfigResult{std::move(cfg)};
}

[[nodiscard]] std::string normalise_separators(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(c == '\\' ? '/' : c);
    }
    return out;
}

/// Glob matcher supporting `**`, `*`, and `?`. `**` matches any sequence of
/// characters including path separators; `*` matches any character except
/// `/`; `?` matches a single character that is not `/`. Backslash is treated
/// as a path separator (normalised to `/` before matching).
///
/// Implemented recursively; pattern lengths are short (a handful of chars in
/// practice) so the stack cost is negligible.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0;
    std::size_t t = 0;

    while (p < pattern.size()) {
        // `**` -- match any sequence of characters, including path
        // separators. Try every possible split point.
        if (pattern[p] == '*' && p + 1 < pattern.size() && pattern[p + 1] == '*') {
            std::size_t pn = p + 2;
            // Consume an optional `/` immediately after `**` so
            // `a/**/b` matches `a/b` as well as `a/x/b`.
            const bool ate_slash = pn < pattern.size() && pattern[pn] == '/';
            if (ate_slash) {
                ++pn;
            }
            const auto rest = pattern.substr(pn);

            // Try matching the rest of the pattern at every position from `t`
            // to end, plus the empty match for the `a/**/b` -> `a/b` case.
            if (glob_match(rest, text.substr(t))) {
                return true;
            }
            for (std::size_t k = t; k < text.size(); ++k) {
                if (glob_match(rest, text.substr(k + 1))) {
                    return true;
                }
            }
            return false;
        }

        // Single `*` -- match any sequence of non-separator characters.
        if (pattern[p] == '*') {
            const auto rest = pattern.substr(p + 1);
            if (glob_match(rest, text.substr(t))) {
                return true;
            }
            for (std::size_t k = t; k < text.size(); ++k) {
                if (text[k] == '/') {
                    return false;  // `*` cannot cross a path separator.
                }
                if (glob_match(rest, text.substr(k + 1))) {
                    return true;
                }
            }
            return false;
        }

        if (t >= text.size()) {
            return false;
        }
        if (pattern[p] == '?') {
            if (text[t] == '/') {
                return false;
            }
            ++p;
            ++t;
            continue;
        }
        if (pattern[p] != text[t]) {
            return false;
        }
        ++p;
        ++t;
    }
    return t == text.size();
}

}  // namespace

bool path_glob_match(std::string_view glob, const std::filesystem::path& path) {
    const auto normalised_path = normalise_separators(path.generic_string());
    const auto normalised_glob = normalise_separators(glob);
    return glob_match(normalised_glob, normalised_path);
}

std::optional<RuleSeverity> Config::severity_for(std::string_view rule_id,
                                                 const std::filesystem::path& file_path) const {
    // [[overrides]] win over [rules]; later overrides win over earlier.
    std::optional<RuleSeverity> override_sev;
    for (const auto& entry : overrides) {
        if (!path_glob_match(entry.path_glob, file_path)) {
            continue;
        }
        const auto it = entry.rule_severity.find(std::string{rule_id});
        if (it != entry.rule_severity.end()) {
            override_sev = it->second;
        }
    }
    if (override_sev.has_value()) {
        return override_sev;
    }

    const auto it = rule_severity.find(std::string{rule_id});
    if (it != rule_severity.end()) {
        return it->second;
    }
    return std::nullopt;
}

ConfigResult load_config_string(std::string_view contents, const std::filesystem::path& origin) {
    // toml++ v3 has two modes: exception-enabled (the default in our build)
    // returns a `toml::table` directly and throws `toml::parse_error` on
    // failure; exception-disabled returns a `toml::parse_result` wrapper. Use
    // try/catch so we work the same way regardless of compile mode.
#if TOML_EXCEPTIONS
    try {
        ::toml::table tbl = ::toml::parse(contents);
        return parse_root(tbl, origin);
    } catch (const ::toml::parse_error& parse_err) {
        return ConfigResult{from_parse_error(parse_err, origin)};
    }
#else
    ::toml::parse_result parsed = ::toml::parse(contents);
    if (!parsed) {
        return ConfigResult{from_parse_error(parsed.error(), origin)};
    }
    return parse_root(parsed.table(), origin);
#endif
}

ConfigResult load_config(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return ConfigResult{make_error("could not open config file", path)};
    }
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    return load_config_string(contents, path);
}

std::optional<std::filesystem::path> find_config(const std::filesystem::path& start) {
    std::error_code ec;
    std::filesystem::path cur = std::filesystem::absolute(start, ec);
    if (ec) {
        cur = start;
    }
    if (std::filesystem::is_regular_file(cur, ec)) {
        cur = cur.parent_path();
    }

    while (true) {
        const auto candidate = cur / ".shader-clippy.toml";
        if (std::filesystem::exists(candidate, ec) &&
            std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }

        // Workspace boundary: stop when we hit a `.git/` (file or dir).
        const auto git_dir = cur / ".git";
        if (std::filesystem::exists(git_dir, ec)) {
            return std::nullopt;
        }

        const auto parent = cur.parent_path();
        if (parent == cur || parent.empty()) {
            return std::nullopt;
        }
        cur = parent;
    }
}

}  // namespace shader_clippy
