// Phase 7 sub-phase 7a.1 IR-engine smoke tests (ADR 0016).
//
// These tests lock in the public-API + orchestrator-routing behaviour of
// the IR pipeline so sub-phase 7a.2 (DXC submodule + real DXIL parser)
// can land its bridge without re-litigating the wire shape:
//
//   1. `Stage::Ir` enumerator exists and is distinct from prior stages.
//   2. `Rule::on_ir` default impl is a no-op (AST-only / reflection-only /
//      CFG-only rules never see it called).
//   3. The orchestrator routes a Stage::Ir rule's `on_ir` invocation
//      correctly: when an enabled rule has `stage() == Stage::Ir` AND
//      `LintOptions::enable_ir == true`, the engine is invoked exactly
//      once per source per lint run.
//   4. `LintOptions::enable_ir == false` short-circuits IR dispatch even
//      when an IR-stage rule is enabled.
//   5. AST-only rule packs pay zero IR cost: when no rule has
//      `stage() == Stage::Ir`, the engine is never invoked and no
//      `clippy::ir-*` diagnostic surfaces.
//   6. Sub-phase 7a.1's stub `analyze()` always returns the
//      `clippy::ir-not-implemented` Note. 7a.2 replaces the body of
//      `analyze()` only; this test will then need to be updated to
//      assert the real `IrInfo` shape.
//   7. `IrInfo` API helpers (`find_function_by_name`,
//      `find_instruction`) are callable on a default-constructed value.

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/ir.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::AstTree;
using shader_clippy::Diagnostic;
using shader_clippy::IrInfo;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::RuleContext;
using shader_clippy::SourceManager;
using shader_clippy::Stage;

/// Spy rule with `stage() == Stage::Ir`. Records each invocation and a
/// snapshot of the `IrInfo` it received. We snapshot rather than store a
/// pointer because the orchestrator's `IrInfo` lives in a temporary inside
/// `lint()` and is destroyed before the test inspects the result.
class IrSpyRule : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return id_;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return "test";
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ir;
    }

    void on_ir(const AstTree& /*tree*/,
               const IrInfo& ir,
               RuleContext& /*ctx*/) override {
        invocations_++;
        last_ir_snapshot_ = ir;  // copy
    }

    [[nodiscard]] std::uint32_t invocations() const noexcept {
        return invocations_;
    }
    [[nodiscard]] const IrInfo& last_ir() const noexcept {
        return last_ir_snapshot_;
    }

private:
    static constexpr std::string_view id_{"test-ir-spy"};
    std::uint32_t invocations_ = 0;
    IrInfo last_ir_snapshot_;
};

/// Spy rule with `stage() == Stage::Ast`. Records `on_node` invocations.
/// Used to assert AST-only rule packs don't trigger the IR engine.
class AstSpyRule : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return id_;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return "test";
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

private:
    static constexpr std::string_view id_{"test-ast-spy"};
};

[[nodiscard]] std::vector<Diagnostic> filter_engine_diagnostics(
    const std::vector<Diagnostic>& diagnostics) {
    std::vector<Diagnostic> kept;
    for (const auto& d : diagnostics) {
        if (d.code.starts_with("clippy::ir")) {
            kept.push_back(d);
        }
    }
    return kept;
}

}  // namespace

TEST_CASE("Stage::Ir is a distinct enum value from Ast / Reflection / ControlFlow",
          "[ir][api]") {
    // Lock in the wire ordering so a future enum reorder doesn't silently
    // shift on-disk integer values rule authors may rely on.
    REQUIRE(static_cast<int>(Stage::Ast) != static_cast<int>(Stage::Ir));
    REQUIRE(static_cast<int>(Stage::Reflection) != static_cast<int>(Stage::Ir));
    REQUIRE(static_cast<int>(Stage::ControlFlow) != static_cast<int>(Stage::Ir));
}

TEST_CASE("IrInfo helpers are callable on a default-constructed value",
          "[ir][api]") {
    const IrInfo info{};
    CHECK(info.functions.empty());
    CHECK(info.target_profile.empty());
    CHECK(info.find_function_by_name("ps_main") == nullptr);
    CHECK(info.find_instruction({}) == nullptr);
}

TEST_CASE("AST-only rule pack: orchestrator never invokes the IR engine",
          "[ir][orchestrator]") {
    SourceManager sources;
    const std::string buf = "float4 ps_main() : SV_Target { return 1.0; }\n";
    const auto src = sources.add_buffer("ast_only.hlsl", buf);
    REQUIRE(src.valid());

    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::make_unique<AstSpyRule>());

    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = false;
    // enable_ir defaults to true, but no IR-stage rule is enabled, so
    // `any_ir_rule()` short-circuits to false and the engine never runs.
    const auto diagnostics = lint(sources, src, rules, options);
    const auto ir_diagnostics = filter_engine_diagnostics(diagnostics);
    CHECK(ir_diagnostics.empty());
}

TEST_CASE("Stage::Ir rule + enable_ir=true: orchestrator dispatches on_ir "
          "with a metadata-only IrInfo (7a.2-step1)",
          "[ir][orchestrator]") {
    // 7a.2-step1 wires `IrEngine::analyze()` against the existing
    // ReflectionEngine: `IrInfo::functions` carries one entry per
    // entry point with `entry_point_name` + `stage`. Per-instruction
    // walks come in 7a.2-step2 (DXC + DXIL parser); this test locks in
    // the metadata-only contract today.
    SourceManager sources;
    // Same shape as the known-working `k_one_cbuffer_one_binding` from
    // test_reflection.cpp. Slang requires at least one declared resource
    // visible to the entry point before its reflection lays down a usable
    // entry-point record; a bare `float4 ps_main()` returns 0 entry points.
    const std::string buf = R"hlsl(
cbuffer SceneConstants
{
    float4x4 view_proj;
};

Texture2D<float4> base_color;
SamplerState linear_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(linear_sampler, uv);
}
)hlsl";
    const auto src = sources.add_buffer("ir_routed.hlsl", buf);
    REQUIRE(src.valid());

    auto spy = std::make_unique<IrSpyRule>();
    IrSpyRule* spy_raw = spy.get();
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::move(spy));

    LintOptions options;
    options.target_profile = std::string{"sm_6_6"};
    options.enable_reflection = false;
    options.enable_control_flow = false;
    options.enable_ir = true;
    const auto diagnostics = lint(sources, src, rules, options);
    const auto ir_diagnostics = filter_engine_diagnostics(diagnostics);

    // The engine succeeds (metadata-only) -- on_ir fires once, no
    // engine diagnostic surfaces. Reflection ran transparently inside
    // `IrEngine::analyze` even though `enable_reflection=false`; that's
    // by design (the reflection cache key + fingerprint are independent
    // of LintOptions and the IR engine drives reflection on its own).
    REQUIRE(spy_raw->invocations() == 1U);
    CHECK(ir_diagnostics.empty());

    const auto& ir = spy_raw->last_ir();
    REQUIRE(!ir.functions.empty());

    // The pixel-stage entry point lives in `IrInfo::functions` keyed by
    // its source name -- `find_function_by_name` is the rule-author API.
    const auto* fn = ir.find_function_by_name("ps_main");
    REQUIRE(fn != nullptr);
    CHECK(fn->stage == "pixel");
    // 7a.2-step1: blocks stay empty -- per-instruction walks come in
    // 7a.2-step2.
    CHECK(fn->blocks.empty());
}

TEST_CASE("Stage::Ir rule + enable_ir=false: orchestrator silently skips IR stage",
          "[ir][orchestrator]") {
    // The user opt-out path. enable_ir=false short-circuits IR dispatch
    // even when a Stage::Ir rule is enabled. Useful for CI runs that want
    // to isolate Phase 7 cost from the rest of the lint pipeline, and for
    // downstream consumers (LSP hot path) that want AST + reflection +
    // CFG only with no IR setup cost.
    SourceManager sources;
    const std::string buf = "float4 ps_main() : SV_Target { return 1.0; }\n";
    const auto src = sources.add_buffer("ir_disabled.hlsl", buf);
    REQUIRE(src.valid());

    auto spy = std::make_unique<IrSpyRule>();
    IrSpyRule* spy_raw = spy.get();
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::move(spy));

    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = false;
    options.enable_ir = false;
    const auto diagnostics = lint(sources, src, rules, options);
    const auto ir_diagnostics = filter_engine_diagnostics(diagnostics);

    CHECK(spy_raw->invocations() == 0U);
    CHECK(ir_diagnostics.empty());
}
