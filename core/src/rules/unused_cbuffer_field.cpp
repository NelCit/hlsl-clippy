// unused-cbuffer-field
//
// Detects cbuffer / ConstantBuffer<T> fields that are declared in reflection
// but appear nowhere in the source-byte text outside of their own declaration.
// This is a coarse approximation -- proper reachability analysis requires
// a CFG (Phase 4 ControlFlow stage). The text-search heuristic catches the
// common case (field added "for completeness" and never read) without false
// positives on names that overlap with intrinsics or keywords, since the
// matcher only counts identifier-bounded occurrences.
//
// Detection plan: for every CBufferField, count occurrences of its identifier
// in the source bytes. The cbuffer's own declaration_span is excluded from
// the count. If the count drops to zero, emit.

#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "unused-cbuffer-field";
constexpr std::string_view k_category = "bindings";

/// Return the number of standalone-identifier occurrences of `name` in `bytes`,
/// ignoring any occurrence whose start byte falls inside `[exclude.lo,
/// exclude.hi)`.
[[nodiscard]] std::size_t count_id_occurrences(std::string_view bytes,
                                               std::string_view name,
                                               ByteSpan exclude) noexcept {
    if (name.empty())
        return 0U;
    std::size_t count = 0U;
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(name, pos);
        if (found == std::string_view::npos)
            return count;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + name.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (ok_left && ok_right) {
            const auto abs = static_cast<std::uint32_t>(found);
            if (abs < exclude.lo || abs >= exclude.hi) {
                ++count;
            }
        }
        pos = found + 1U;
    }
    return count;
}

class UnusedCBufferField : public Rule {
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
        const auto bytes = tree.source_bytes();
        for (const auto& cb : reflection.cbuffers) {
            for (const auto& field : cb.fields) {
                const auto refs =
                    count_id_occurrences(bytes, field.name, cb.declaration_span.bytes);
                if (refs != 0U)
                    continue;
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = cb.declaration_span;
                diag.message = std::string{"cbuffer field `"} + cb.name + "." + field.name +
                               "` is declared but never read -- remove it or move it to a "
                               "different cbuffer to recover " +
                               std::to_string(field.byte_size) + " bytes";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_unused_cbuffer_field() {
    return std::make_unique<UnusedCBufferField>();
}

}  // namespace hlsl_clippy::rules
