#pragma once

#include <string>
#include <vector>

#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {

/// Severity classes follow the rustc/clippy convention. `Note` is informational
/// only; `Warning` is the typical rule firing; `Error` is reserved for
/// correctness violations or rule-engine failures.
enum class Severity {
    Note,
    Warning,
    Error,
};

/// Placeholder for Phase 1's machine-applicable rewrite engine. Phase 0 rules
/// emit diagnostics with an empty `edits` vector; the type exists so that the
/// `Diagnostic` schema is forward-compatible.
struct TextEdit {
    Span span;  ///< Range of source bytes to replace.
    std::string replacement;
};

struct Fix {
    /// Human-readable description of what the fix would do.
    std::string description;
    /// Sequence of edits applied atomically.
    std::vector<TextEdit> edits;
};

struct Diagnostic {
    /// Stable rule identifier, e.g. `"pow-const-squared"`.
    std::string code;
    Severity severity = Severity::Warning;
    /// Primary span underlying the diagnostic.
    Span primary_span;
    /// Single-line message, no trailing newline.
    std::string message;
    /// Optional fixes. Phase 0 rules leave this empty.
    std::vector<Fix> fixes;
};

}  // namespace hlsl_clippy
