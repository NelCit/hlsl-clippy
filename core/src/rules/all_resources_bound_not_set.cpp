// all-resources-bound-not-set
//
// Detects shaders that declare a non-trivial resource set but do not contain
// the `[RootSignature(...)]` attribute or any indication that the shader was
// compiled with `-all-resources-bound`. The flag unlocks driver
// optimisations on RDNA / Turing / Ada by promising the runtime that every
// declared resource is actually bound -- without it, the driver inserts
// per-resource null-binding checks on the hot path.
//
// Detection plan: this is a project-level rule. We have no way to inspect
// the shader's compile flags from the linter, so we approximate with a
// reflection-driven heuristic: when reflection reports >= 4 resource
// bindings (a non-trivial root signature shape) and the source bytes do
// NOT contain the literal token `RootSignature` (case-sensitive), emit a
// note pointing the user at the flag. Severity is `Note` because the rule
// surfaces a project-policy hint, not a defect in the shader itself.

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
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "all-resources-bound-not-set";
constexpr std::string_view k_category = "bindings";
constexpr std::size_t k_min_bindings = 4U;

class AllResourcesBoundNotSet : public Rule {
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
        if (reflection.bindings.size() < k_min_bindings)
            return;
        const auto bytes = tree.source_bytes();
        if (bytes.find("RootSignature") != std::string_view::npos)
            return;
        // Anchor the diagnostic at the first binding's declaration span when
        // available, otherwise at the start of the file.
        Span anchor{.source = tree.source_id(), .bytes = ByteSpan{0U, 0U}};
        if (!reflection.bindings.empty() &&
            reflection.bindings.front().declaration_span.size() > 0U) {
            anchor.bytes = reflection.bindings.front().declaration_span;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Note;
        diag.primary_span = anchor;
        diag.message = std::string{"shader declares "} +
                       std::to_string(reflection.bindings.size()) +
                       " resource bindings but no `[RootSignature(...)]` attribute is present -- "
                       "compiling with `-all-resources-bound` (D3D12) unlocks driver "
                       "optimisations that skip per-resource null-binding checks on every IHV";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_all_resources_bound_not_set() {
    return std::make_unique<AllResourcesBoundNotSet>();
}

}  // namespace hlsl_clippy::rules
