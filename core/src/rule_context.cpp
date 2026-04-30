#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/suppress.hpp"

namespace hlsl_clippy {

void RuleContext::emit(Diagnostic diag) {
    if (suppressions_ != nullptr &&
        suppressions_->suppresses(
            diag.code, diag.primary_span.bytes.lo, diag.primary_span.bytes.hi)) {
        return;
    }
    diagnostics_.push_back(std::move(diag));
}

std::vector<Diagnostic> RuleContext::take_diagnostics() noexcept {
    return std::move(diagnostics_);
}

// Default `on_node` is a no-op; rules that need to ignore certain node types
// don't have to override.
void Rule::on_node(const AstCursor& /*cursor*/, RuleContext& /*ctx*/) {}

}  // namespace hlsl_clippy
