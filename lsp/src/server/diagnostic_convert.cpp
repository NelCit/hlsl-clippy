// Diagnostic conversion implementation.

#include "server/diagnostic_convert.hpp"

#include <cstdint>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::lsp::server {

namespace {

/// LSP severity numbers (per the spec).
constexpr int k_lsp_severity_error = 1;
constexpr int k_lsp_severity_warning = 2;
constexpr int k_lsp_severity_information = 3;

[[nodiscard]] int to_lsp_severity(hlsl_clippy::Severity sev) noexcept {
    switch (sev) {
        case hlsl_clippy::Severity::Error:
            return k_lsp_severity_error;
        case hlsl_clippy::Severity::Warning:
            return k_lsp_severity_warning;
        case hlsl_clippy::Severity::Note:
            return k_lsp_severity_information;
    }
    return k_lsp_severity_warning;
}

[[nodiscard]] nlohmann::json to_lsp_position(const hlsl_clippy::SourceManager& sources,
                                             hlsl_clippy::SourceId source_id,
                                             std::uint32_t byte_offset) {
    const auto loc = sources.resolve(source_id, byte_offset);
    nlohmann::json obj = nlohmann::json::object();
    // hlsl_clippy uses 1-based line/column; LSP uses 0-based.
    obj["line"] = loc.line > 0U ? loc.line - 1U : 0U;
    obj["character"] = loc.column > 0U ? loc.column - 1U : 0U;
    return obj;
}

}  // namespace

nlohmann::json to_lsp_diagnostic(const hlsl_clippy::Diagnostic& diag,
                                 const hlsl_clippy::SourceManager& sources) {
    nlohmann::json obj = nlohmann::json::object();

    nlohmann::json range = nlohmann::json::object();
    range["start"] = to_lsp_position(sources, diag.primary_span.source, diag.primary_span.bytes.lo);
    range["end"] = to_lsp_position(sources, diag.primary_span.source, diag.primary_span.bytes.hi);
    obj["range"] = std::move(range);

    obj["severity"] = to_lsp_severity(diag.severity);
    obj["code"] = diag.code;
    obj["source"] = "hlsl-clippy";
    obj["message"] = diag.message;
    return obj;
}

nlohmann::json to_lsp_diagnostics(const std::vector<hlsl_clippy::Diagnostic>& diagnostics,
                                  const hlsl_clippy::SourceManager& sources) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : diagnostics) {
        arr.push_back(to_lsp_diagnostic(d, sources));
    }
    return arr;
}

}  // namespace hlsl_clippy::lsp::server
