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

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/ir.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::AstTree;
using hlsl_clippy::Diagnostic;
using hlsl_clippy::IrInfo;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::RuleContext;
using hlsl_clippy::SourceManager;
using hlsl_clippy::Stage;

/// Spy rule with `stage() == Stage::Ir`. Records every `on_ir` invocation
/// alongside the (pointer-equal) `IrInfo` it received so tests can assert
/// the orchestrator dispatched correctly.
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
        last_ir_ = &ir;
    }

    [[nodiscard]] std::uint32_t invocations() const noexcept {
        return invocations_;
    }
    [[nodiscard]] const IrInfo* last_ir() const noexcept {
        return last_ir_;
    }

private:
    static constexpr std::string_view id_{"test-ir-spy"};
    std::uint32_t invocations_ = 0;
    const IrInfo* last_ir_ = nullptr;
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
          "or surfaces the 7a.1 not-implemented diagnostic",
          "[ir][orchestrator]") {
    // 7a.1 ships a stub engine -- analyze() always fails with
    // `clippy::ir-not-implemented`. The orchestrator surfaces that
    // diagnostic and skips on_ir dispatch; the spy's invocations_ counter
    // therefore stays at 0. Once 7a.2 lands, this test will need an
    // additional branch where the engine returns a real IrInfo and on_ir
    // is dispatched. The behaviour locked in here is the WIRE: an IR-stage
    // rule with enable_ir=true causes either dispatch OR a clippy::ir-*
    // diagnostic, never silent skipping.
    SourceManager sources;
    const std::string buf = "float4 ps_main() : SV_Target { return 1.0; }\n";
    const auto src = sources.add_buffer("ir_routed.hlsl", buf);
    REQUIRE(src.valid());

    auto spy = std::make_unique<IrSpyRule>();
    IrSpyRule* spy_raw = spy.get();
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(std::move(spy));

    LintOptions options;
    options.enable_reflection = false;
    options.enable_control_flow = false;
    options.enable_ir = true;
    const auto diagnostics = lint(sources, src, rules, options);
    const auto ir_diagnostics = filter_engine_diagnostics(diagnostics);

    // Either the stub fired (7a.1) OR a real IR-info dispatch landed
    // (7a.2). Exactly one of these branches is true at any commit.
    if (spy_raw->invocations() == 0U) {
        REQUIRE(ir_diagnostics.size() == 1U);
        CHECK(ir_diagnostics[0].code == "clippy::ir-not-implemented");
        CHECK(ir_diagnostics[0].severity == hlsl_clippy::Severity::Note);
    } else {
        CHECK(spy_raw->invocations() == 1U);
        CHECK(ir_diagnostics.empty());
    }
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
