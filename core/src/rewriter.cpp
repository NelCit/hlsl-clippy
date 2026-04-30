// Quick-fix Rewriter implementation per ADR 0008 §3.
//
// Algorithm:
// 1. Flatten every `PrioritisedFix` into a list of `(fix-index, priority,
//    edits)` records where each fix carries a stable index into the input.
// 2. Sort the fixes by descending priority (severity rank, then rule_id
//    tiebreaker, then input order). This makes higher-priority fixes win
//    overlap resolution.
// 3. Walk the sorted list and accept a fix iff none of its edits overlaps an
//    already-accepted edit's byte range. Multi-edit fixes are atomic — one
//    overlap drops the whole fix.
// 4. Sort the accepted edits by descending byte_lo; apply them in that order
//    to the source so byte offsets in unprocessed prefix bytes never shift.
// 5. Emit one FixConflict per dropped fix.

#include "hlsl_clippy/rewriter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"

namespace hlsl_clippy {

namespace {

[[nodiscard]] int severity_rank(Severity s) noexcept {
    // Errors win, warnings next, notes last.
    switch (s) {
        case Severity::Error:
            return 2;
        case Severity::Warning:
            return 1;
        case Severity::Note:
            return 0;
    }
    return 0;
}

/// Strict-weak ordering for priorities: higher severity first, then
/// lexicographic rule_id, then input index.
[[nodiscard]] bool priority_greater(const FixPriority& a,
                                    std::size_t a_idx,
                                    const FixPriority& b,
                                    std::size_t b_idx) noexcept {
    const auto ra = severity_rank(a.severity);
    const auto rb = severity_rank(b.severity);
    if (ra != rb) {
        return ra > rb;
    }
    if (a.rule_id != b.rule_id) {
        return a.rule_id < b.rule_id;
    }
    return a_idx < b_idx;
}

/// Half-open `[lo, hi)` overlap test. Touching at an endpoint is NOT an
/// overlap (a fix can replace `[5, 10)` while another inserts at `[10, 12)`).
[[nodiscard]] bool overlaps(std::uint32_t a_lo,
                            std::uint32_t a_hi,
                            std::uint32_t b_lo,
                            std::uint32_t b_hi) noexcept {
    return a_lo < b_hi && b_lo < a_hi;
}

}  // namespace

std::string Rewriter::apply(std::string_view source,
                            std::span<const PrioritisedFix> fixes,
                            std::vector<FixConflict>* conflicts_out) const {
    if (fixes.empty()) {
        return std::string{source};
    }

    // (1) Stable index list, sorted by priority.
    std::vector<std::size_t> order;
    order.reserve(fixes.size());
    for (std::size_t i = 0; i < fixes.size(); ++i) {
        order.push_back(i);
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) noexcept {
        return priority_greater(fixes[a].priority, a, fixes[b].priority, b);
    });

    // (2) Accept fixes in priority order, recording the byte ranges and
    // owning rule id for each accepted edit. A new fix is accepted iff none
    // of its edits overlaps any already-accepted edit.
    struct AcceptedEdit {
        std::uint32_t byte_lo;
        std::uint32_t byte_hi;
        std::string replacement;
        std::string owner_rule_id;  ///< rule_id of the fix that produced this edit.
    };
    std::vector<AcceptedEdit> accepted_edits;
    accepted_edits.reserve(fixes.size());

    auto find_overlap = [&](const Fix& f) noexcept -> const AcceptedEdit* {
        for (const auto& e : f.edits) {
            for (const auto& ae : accepted_edits) {
                if (overlaps(e.span.bytes.lo, e.span.bytes.hi, ae.byte_lo, ae.byte_hi)) {
                    return &ae;
                }
            }
        }
        return nullptr;
    };

    for (const std::size_t idx : order) {
        const auto& pf = fixes[idx];
        if (pf.fix.edits.empty()) {
            continue;
        }
        if (const AcceptedEdit* clash = find_overlap(pf.fix); clash != nullptr) {
            if (conflicts_out != nullptr) {
                FixConflict conflict;
                conflict.dropped_rule_id = pf.priority.rule_id;
                conflict.winning_rule_id = clash->owner_rule_id;
                conflict.byte_lo = pf.fix.edits.front().span.bytes.lo;
                conflict.byte_hi = pf.fix.edits.front().span.bytes.hi;
                conflicts_out->push_back(std::move(conflict));
            }
            continue;
        }

        for (const auto& e : pf.fix.edits) {
            accepted_edits.push_back(AcceptedEdit{.byte_lo = e.span.bytes.lo,
                                                  .byte_hi = e.span.bytes.hi,
                                                  .replacement = e.replacement,
                                                  .owner_rule_id = pf.priority.rule_id});
        }
    }

    // (3) Apply accepted edits in reverse byte order so unmodified prefix
    // offsets remain valid as we splice.
    std::ranges::sort(accepted_edits, [](const AcceptedEdit& a, const AcceptedEdit& b) noexcept {
        return a.byte_lo > b.byte_lo;
    });

    std::string out{source};
    for (const auto& e : accepted_edits) {
        if (e.byte_lo > out.size() || e.byte_hi > out.size() || e.byte_hi < e.byte_lo) {
            continue;
        }
        out.replace(e.byte_lo, static_cast<std::size_t>(e.byte_hi - e.byte_lo), e.replacement);
    }
    return out;
}

}  // namespace hlsl_clippy
