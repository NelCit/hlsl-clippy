// Public IR types exposed to rule authors.
//
// This header is the only public surface for IR-level analysis in the lint
// engine. Per ADR 0016, no DXIL parser handles, `<DxilContainer.h>` types, or
// LLVM types are allowed to leak across the public API boundary -- every
// type defined here is a copyable / movable value type that the IR engine
// populates by walking the parsed DXIL once per (SourceId, target_profile)
// tuple.
//
// Rule authors with `stage() == Stage::Ir` receive a `const IrInfo&` in their
// `on_ir` hook and use the helpers below to answer questions like "what
// instructions are live across this `TraceRay`?" or "is this `Sample` call
// inside the same basic block as another identical sample?". They never see
// DXIL directly.
//
// Sub-phase 7a.1 (the API foundation that ships first) ships these public
// types and a stub `IrEngine` that always surfaces a single
// `clippy::ir-not-implemented` warn-severity diagnostic. Sub-phase 7a.2
// lands the DXC submodule + real DXIL parser + debug-info projection.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shader_clippy/source.hpp"

namespace shader_clippy {

/// Coarse-grained IR opcode tags rules switch on. The IR engine maps DXIL
/// LLVM opcodes onto this enum; rules never see raw DXIL. New opcodes are
/// added incrementally as Phase 7 rule packs need finer tags -- ADR 0016
/// commits only to the rough taxonomy below.
enum class IrOpcode : std::uint16_t {
    Unknown = 0,

    // Memory.
    Load,
    Store,
    AllocaLocalArray,
    DynamicGep,

    // Texture intrinsics.
    Sample,
    SampleLevel,
    SampleGrad,
    SampleCmp,
    Gather,

    // Ray tracing (DXR).
    TraceRay,
    AcceptHitAndEndSearch,
    IgnoreHit,
    ReportHit,
    CallShader,

    // SM 6.9 SER + HitObject.
    MaybeReorderThread,
    HitObjectInvoke,
    HitObjectTraceRay,
    HitObjectFromRayQuery,

    // Mesh / amplification.
    MeshSetOutputCounts,
    MeshOutputVertex,
    MeshOutputPrimitive,
    DispatchMesh,

    // ALU / conversions.
    AluFloat,
    AluInt,
    Bitcast,
    F32ToF16,
    F16ToF32,
    Pack8888,
    Unpack8888,

    // Control flow.
    Phi,
    Branch,
    Switch,
    Return,
};

/// Opaque handle for an IR function. The numeric value is an internal
/// index into the engine's per-source function table; it is stable for the
/// lifetime of the `IrInfo` it came from but is NOT comparable across
/// different `IrInfo` values.
class IrFunctionId {
public:
    constexpr IrFunctionId() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(IrFunctionId lhs, IrFunctionId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

private:
    friend class IrEngineFactory;  // implementation-side constructor
    explicit constexpr IrFunctionId(std::uint32_t v) noexcept : value_(v) {}
    std::uint32_t value_ = 0;
};

/// Opaque handle for an IR basic block. Same lifetime/comparability rules
/// as `IrFunctionId`.
class IrBasicBlockId {
public:
    constexpr IrBasicBlockId() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(IrBasicBlockId lhs,
                                                   IrBasicBlockId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

private:
    friend class IrEngineFactory;
    explicit constexpr IrBasicBlockId(std::uint32_t v) noexcept : value_(v) {}
    std::uint32_t value_ = 0;
};

/// Opaque handle for an IR instruction. Same lifetime/comparability rules
/// as `IrFunctionId`.
class IrInstructionId {
public:
    constexpr IrInstructionId() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(IrInstructionId lhs,
                                                   IrInstructionId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

private:
    friend class IrEngineFactory;
    explicit constexpr IrInstructionId(std::uint32_t v) noexcept : value_(v) {}
    std::uint32_t value_ = 0;
};

/// One IR instruction. The `span` is a best-effort source anchor recovered
/// from DXIL debug-info; instructions without debug-info anchor to the
/// enclosing entry point's declaration span (never invented).
struct IrInstruction {
    IrInstructionId id{};
    IrOpcode opcode = IrOpcode::Unknown;
    /// Best-effort source anchor; see DocComment above.
    Span span{};
    /// Operand instruction IDs in their IR order. Empty for terminators that
    /// take no value operands (e.g. unconditional `Branch`).
    std::vector<IrInstructionId> operands;
    /// Result bit-width when the instruction produces a value (16, 32, 64).
    /// `nullopt` when the instruction has no SSA result (`Store`, terminators).
    std::optional<std::uint32_t> result_bit_width;
};

/// One IR basic block. Successor edges are explicit; the IR builder follows
/// the DXIL CFG (which mirrors the LLVM CFG).
struct IrBasicBlock {
    IrBasicBlockId id{};
    std::vector<IrInstructionId> instructions;
    std::vector<IrBasicBlockId> successors;
};

/// One IR function -- one entry point or one inlined helper. `stage` matches
/// the lowercase tag used in `EntryPointInfo::stage` (Phase 3): `"compute"`,
/// `"raygeneration"`, `"closesthit"`, `"mesh"`, etc.
struct IrFunction {
    IrFunctionId id{};
    std::string entry_point_name;
    std::string stage;
    std::vector<IrBasicBlock> blocks;
    /// Span of the entry-point's declaration in source. Used as the fallback
    /// anchor for instructions that lack debug-info (per ADR 0016
    /// §"Stable spans via debug-info round-tripping").
    Span declaration_span{};
};

/// IR view for one (SourceId, target_profile) tuple. Owned by the
/// `IrEngine`'s per-lint-run cache; rules consume a `const IrInfo&` via the
/// `on_ir` hook.
struct IrInfo {
    /// Functions in IR order. Entry points come first; helper functions
    /// (when the bridge inlines them) follow.
    std::vector<IrFunction> functions;
    /// Target profile this IR was compiled against, e.g. `"sm_6_6"`. Rules
    /// that are SM-version-gated (e.g. SM 6.9 SER rules) read this to skip
    /// emission on older profiles.
    std::string target_profile;

    /// Linear lookup by entry-point name. Returns `nullptr` if not found.
    [[nodiscard]] const IrFunction* find_function_by_name(std::string_view name) const noexcept;

    /// Linear lookup by instruction id. Returns `nullptr` if `id` is not
    /// from this `IrInfo`.
    [[nodiscard]] const IrInstruction* find_instruction(IrInstructionId id) const noexcept;
};

}  // namespace shader_clippy
