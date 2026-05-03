#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shader_clippy {

/// One inline-suppression annotation parsed from a `// shader-clippy: allow(...)`
/// comment. Spans are half-open `[byte_lo, byte_hi)` byte ranges into the
/// source buffer. The wildcard rule id `"*"` matches any rule.
struct Suppression {
    std::string rule_id;        ///< Rule name, or `"*"` for wildcard.
    std::uint32_t byte_lo = 0;  ///< First byte of the suppressed range.
    std::uint32_t byte_hi = 0;  ///< One past the last byte.
};

/// Result of scanning a source file for suppression annotations. Look-up is
/// O(log N + matches) per query via per-rule sorted interval lists.
class SuppressionSet {
public:
    SuppressionSet() = default;

    /// Scan `source` for `// shader-clippy: allow(...)` annotations. The scanner
    /// is grammar-agnostic — it walks raw bytes, tracks a small state machine
    /// for line/block comments and string literals, and resolves each
    /// annotation's scope from the surrounding code.
    [[nodiscard]] static SuppressionSet scan(std::string_view source);

    /// True if at least one active suppression covers the given byte range.
    /// `byte_lo` and `byte_hi` are half-open. The rule id `"*"` is implicitly
    /// matched: a wildcard suppression suppresses every rule.
    [[nodiscard]] bool suppresses(std::string_view rule_id,
                                  std::uint32_t byte_lo,
                                  std::uint32_t byte_hi) const noexcept;

    /// All suppressions, in source order. Useful for tooling and `--debug`.
    [[nodiscard]] const std::vector<Suppression>& entries() const noexcept {
        return entries_;
    }

    /// Diagnostics emitted by the scanner itself (e.g. malformed annotations).
    /// Each is a single-line message with a byte offset relative to the
    /// scanned source. The driver folds these into the lint pass output.
    struct ScanDiagnostic {
        std::string message;
        std::uint32_t byte_lo = 0;
        std::uint32_t byte_hi = 0;
    };

    [[nodiscard]] const std::vector<ScanDiagnostic>& scan_diagnostics() const noexcept {
        return scan_diagnostics_;
    }

private:
    void index();

    std::vector<Suppression> entries_;
    std::vector<ScanDiagnostic> scan_diagnostics_;
    // Per-rule interval list, sorted by lo for binary-search look-ups.
    std::unordered_map<std::string, std::vector<std::pair<std::uint32_t, std::uint32_t>>> by_rule_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> wildcard_;
};

}  // namespace shader_clippy
