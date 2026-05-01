// cbuffer-large-fits-rootcbv-not-table
//
// Detects a cbuffer whose total size is too large to fit in root constants
// (>32 bytes / 8 dwords) but small enough to remain within the 64 KB cbuffer
// limit, and which therefore would be a candidate for binding as a root CBV
// rather than via a descriptor table. Root CBVs save a descriptor-heap
// dereference on every IHV; for a cbuffer referenced once per dispatch the
// indirection is pure overhead.
//
// Detection (Reflection-stage):
//   For each `CBufferLayout` in the reflection result, check that
//     32 < total_bytes <= 65536
//   and emit a suggestion-grade diagnostic anchored at the cbuffer's
//   declaration span. No fix -- the choice between a root CBV and a
//   descriptor table is a root-signature decision the linter cannot make
//   on the developer's behalf.
//
// Companion to the (locked) `cbuffer-fits-rootconstants` rule which targets
// the small end of the same spectrum.
//
// Stage: Reflection (uses CBufferLayout::total_bytes from Slang reflection).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "cbuffer-large-fits-rootcbv-not-table";
constexpr std::string_view k_category = "bindings";

// Thresholds in bytes.
//   - Root constants top out at 64 dwords (256 bytes) total in the root sig,
//     but the per-cbuffer "small" boundary used by `cbuffer-fits-rootconstants`
//     is 32 bytes / 8 dwords (mirroring the shipped rule's gpu reason).
//   - HLSL constant buffers are capped at 64 KB.
constexpr std::uint32_t k_rootconst_max_bytes = 32U;
constexpr std::uint32_t k_cbuffer_max_bytes = 65536U;

class CBufferLargeFitsRootCbvNotTable : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        for (const auto& cb : reflection.cbuffers) {
            if (cb.total_bytes <= k_rootconst_max_bytes) {
                continue;
            }
            if (cb.total_bytes > k_cbuffer_max_bytes) {
                continue;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            // Anchor at the cbuffer's declaration span if reflection populated
            // one; otherwise fall back to a zero-length span at byte 0 so the
            // diagnostic still renders.
            if (cb.declaration_span.bytes.hi > 0) {
                diag.primary_span = cb.declaration_span;
            } else {
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            }
            diag.message = std::string{"cbuffer `"} + cb.name + std::string{"` ("} +
                           std::to_string(cb.total_bytes) +
                           std::string{
                               " bytes) is too large for root constants but well under the 64 KB "
                               "cbuffer limit -- consider binding as a root CBV to dodge the "
                               "descriptor-table indirection on every dispatch"};
            // Suggestion-grade: no machine-applicable fix. The root-signature
            // choice belongs to the host-side pipeline-state-object code.
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_cbuffer_large_fits_rootcbv_not_table() {
    return std::make_unique<CBufferLargeFitsRootCbvNotTable>();
}

}  // namespace hlsl_clippy::rules
