#include <cstdint>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/ir.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/suppress.hpp"

#include "parser_internal.hpp"

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

// Default `on_tree` is a no-op; imperative rules ignore it entirely.
void Rule::on_tree(const AstTree& /*tree*/, RuleContext& /*ctx*/) {}

// Default `on_reflection` is a no-op; AST-only rules never see it called
// (the orchestrator only dispatches `on_reflection` for `Stage::Reflection`
// rules), and reflection rules override this hook.
void Rule::on_reflection(const AstTree& /*tree*/,
                         const ReflectionInfo& /*reflection*/,
                         RuleContext& /*ctx*/) {}

// Default `on_cfg` is a no-op; AST-only and reflection-only rules never see
// it called (the orchestrator only dispatches `on_cfg` for
// `Stage::ControlFlow` rules), and control-flow rules override this hook.
void Rule::on_cfg(const AstTree& /*tree*/, const ControlFlowInfo& /*cfg*/, RuleContext& /*ctx*/) {}

// Default `on_ir` is a no-op; AST / reflection / control-flow-only rules
// never see it called (the orchestrator only dispatches `on_ir` for
// `Stage::Ir` rules), and IR rules override this hook (ADR 0016).
void Rule::on_ir(const AstTree& /*tree*/, const IrInfo& /*ir*/, RuleContext& /*ctx*/) {}

// IrInfo helpers -- linear scans against function/instruction tables. Rule
// authors call these once per emit; the cardinality is bounded by the source
// (a typical compute shader has 1-3 entry points and dozens to hundreds of
// instructions per function), so a flat scan is the right shape.
const IrFunction* IrInfo::find_function_by_name(std::string_view name) const noexcept {
    for (const auto& fn : functions) {
        if (fn.entry_point_name == name) {
            return &fn;
        }
    }
    return nullptr;
}

const IrInstruction* IrInfo::find_instruction(IrInstructionId /*id*/) const noexcept {
    // 7a.1 ships with an empty `IrInfo` -- no functions, no instructions --
    // because the DXC parser has not landed yet. The linear walk would have
    // nothing to find. 7a.2 will populate `IrFunction::blocks` with
    // `IrInstruction` records and add a `std::flat_map<IrInstructionId,
    // const IrInstruction*>` lookup table as a sibling of `functions`.
    // Until then this returns nullptr unconditionally; rules that depend on
    // it will be wired in 7a.2 alongside the engine.
    return nullptr;
}

std::string_view AstTree::text(::TSNode node) const noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (hi > source_bytes_.size() || hi < lo) {
        return {};
    }
    return source_bytes_.substr(lo, hi - lo);
}

ByteSpan AstTree::byte_range(::TSNode node) const noexcept {
    if (::ts_node_is_null(node)) {
        return ByteSpan{};
    }
    return ByteSpan{
        .lo = static_cast<std::uint32_t>(::ts_node_start_byte(node)),
        .hi = static_cast<std::uint32_t>(::ts_node_end_byte(node)),
    };
}

}  // namespace hlsl_clippy
