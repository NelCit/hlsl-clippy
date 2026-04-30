#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"

namespace hlsl_clippy {

/// Priority assigned to a `Fix` when resolving overlap conflicts. Higher
/// numbers win. The driver computes this from the host diagnostic's severity
/// (errors beat warnings beat notes) with `rule_id` as a tiebreaker.
struct FixPriority {
    Severity severity = Severity::Warning;
    /// Stable identifier — typically the rule id. Used to break ties between
    /// equally-severe overlapping fixes deterministically.
    std::string rule_id;
};

/// One fix-with-priority pair handed to the Rewriter.
struct PrioritisedFix {
    FixPriority priority;
    Fix fix;
};

/// A conflict surfaced by the Rewriter when two fixes' edits overlap and the
/// lower-priority one was dropped. The driver should emit a `note`-severity
/// diagnostic per conflict so users see why a fix was skipped.
struct FixConflict {
    /// rule_id of the dropped fix.
    std::string dropped_rule_id;
    /// rule_id of the fix that won the overlap.
    std::string winning_rule_id;
    /// Byte range of the dropped fix's first edit; useful for human output.
    std::uint32_t byte_lo = 0;
    std::uint32_t byte_hi = 0;
};

/// Rewriter applies a set of `Fix`es to a single source buffer. Multi-edit
/// fixes are atomic: if any edit conflicts with a higher-priority edit, the
/// entire fix is dropped.
class Rewriter {
public:
    /// Apply `fixes` to `source` and return the rewritten string. Conflicts
    /// (overlapping edits across different priorities) are written into
    /// `conflicts_out` if non-null; the lower-priority fix is dropped.
    /// Edits inside one fix that overlap each other are not handled (they're
    /// the rule author's responsibility to keep disjoint).
    [[nodiscard]] std::string apply(std::string_view source,
                                    std::span<const PrioritisedFix> fixes,
                                    std::vector<FixConflict>* conflicts_out = nullptr) const;
};

}  // namespace hlsl_clippy
