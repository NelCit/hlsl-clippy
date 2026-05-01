// tools/slang-smoke/src/main.cpp
//
// Minimal smoke test: compile two HLSL shaders through Slang and report
// diagnostics.  Expected outcomes:
//   [OK]  good shader compiles with zero or more informational diagnostics.
//   [ERR] bad shader fails to compile and diagnostic text is printed.
//
// Exit 0 on success (good compiled OK, bad failed as expected).
// Exit non-zero if either expectation is violated.

#include <cstdio>
#include <cstring>
#include <string_view>

#include <slang-com-ptr.h>
#include <slang.h>

// ---------------------------------------------------------------------------
// HLSL source strings
// ---------------------------------------------------------------------------

static constexpr std::string_view k_good_source{R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
)hlsl"};

// Deliberate typo: `ddxx` is not a valid HLSL intrinsic.
static constexpr std::string_view k_bad_source{R"hlsl(
[shader("pixel")]
float4 ps_bad(float2 uv : TEXCOORD0) : SV_Target
{
    float2 d = ddxx(uv);   // error: 'ddxx' is not a known intrinsic
    return float4(d, 0.0f, 1.0f);
}
)hlsl"};

// ---------------------------------------------------------------------------
// Helper: extract diagnostic text from a blob (may be nullptr).
// ---------------------------------------------------------------------------

static std::string_view diag_text(slang::IBlob* blob) noexcept {
    if (blob == nullptr) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- Slang API
    return {reinterpret_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
}

// ---------------------------------------------------------------------------
// Compile one shader and report results.
// Returns true when compilation succeeded.
// ---------------------------------------------------------------------------

static bool compile_shader(slang::ISession* session,
                           std::string_view module_name,
                           std::string_view entry_name,
                           std::string_view source,
                           bool expect_success) {
    Slang::ComPtr<slang::IBlob> load_diag;

    // loadModuleFromSourceString takes a null-terminated C string.
    // Our string_view literals are already null-terminated, but we pass
    // .data() to be explicit.
    slang::IModule* raw_module =
        session->loadModuleFromSourceString(module_name.data(),
                                            module_name.data(),  // virtual path == module name
                                            source.data(),
                                            load_diag.writeRef());

    if (raw_module == nullptr) {
        // Load / parse failed.
        const std::string_view diag = diag_text(load_diag.get());
        std::printf("[ERR] %.*s: load failed (%zu diagnostics chars)\n%.*s\n",
                    static_cast<int>(entry_name.size()),
                    entry_name.data(),
                    diag.size(),
                    static_cast<int>(diag.size()),
                    diag.data());
        return !expect_success;  // failing is OK when we expected failure
    }

    // Wrap the raw pointer so COM ref-counting is correct.
    // loadModuleFromSourceString returns a borrowed pointer (session owns it),
    // but ComPtr's addRef behaviour is fine here.
    Slang::ComPtr<slang::IModule> module;
    module.attach(raw_module);
    raw_module->addRef();

    // Find the entry point.
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    Slang::ComPtr<slang::IBlob> ep_diag;
    const SlangResult ep_res = module->findAndCheckEntryPoint(
        entry_name.data(), SLANG_STAGE_PIXEL, entry_point.writeRef(), ep_diag.writeRef());

    if (SLANG_FAILED(ep_res) || entry_point == nullptr) {
        const std::string_view diag = diag_text(ep_diag.get());
        std::printf("[ERR] %.*s: entry-point lookup failed\n%.*s\n",
                    static_cast<int>(entry_name.size()),
                    entry_name.data(),
                    static_cast<int>(diag.size()),
                    diag.data());
        return !expect_success;
    }

    // Link: entry-point → program.
    Slang::ComPtr<slang::IComponentType> linked;
    Slang::ComPtr<slang::IBlob> link_diag;
    const SlangResult link_res = entry_point->link(linked.writeRef(), link_diag.writeRef());

    if (SLANG_FAILED(link_res) || linked == nullptr) {
        const std::string_view diag = diag_text(link_diag.get());
        std::printf("[ERR] %.*s: %zu diagnostics\n%.*s\n",
                    static_cast<int>(entry_name.size()),
                    entry_name.data(),
                    diag.size(),
                    static_cast<int>(diag.size()),
                    diag.data());
        return !expect_success;
    }

    // Generate target code (DXIL, target index 0).
    Slang::ComPtr<slang::IBlob> code;
    Slang::ComPtr<slang::IBlob> code_diag;
    const SlangResult code_res =
        linked->getEntryPointCode(0, 0, code.writeRef(), code_diag.writeRef());

    const std::string_view diag = diag_text(code_diag.get());

    if (SLANG_FAILED(code_res) || code == nullptr) {
        std::printf("[ERR] %.*s: %zu diagnostics\n%.*s\n",
                    static_cast<int>(entry_name.size()),
                    entry_name.data(),
                    diag.size(),
                    static_cast<int>(diag.size()),
                    diag.data());
        return !expect_success;
    }

    // Success path.
    const std::size_t n_diag_chars = diag.size();
    std::printf("[OK]  %.*s compiled, %zu diagnostic char(s)\n",
                static_cast<int>(entry_name.size()),
                entry_name.data(),
                n_diag_chars);

    return expect_success;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // 1. Create a global session.
    Slang::ComPtr<slang::IGlobalSession> global_session;
    {
        const SlangResult res = slang::createGlobalSession(global_session.writeRef());
        if (SLANG_FAILED(res)) {
            std::fputs("slang-smoke: failed to create global session\n", stderr);
            return 1;
        }
    }

    // 2. Describe a DXIL / sm_6_0 target.
    slang::TargetDesc target_desc{};
    target_desc.format = SLANG_DXIL;
    target_desc.profile = global_session->findProfile("sm_6_0");

    if (target_desc.profile == SLANG_PROFILE_UNKNOWN) {
        std::fputs("slang-smoke: profile 'sm_6_0' not found\n", stderr);
        return 1;
    }

    slang::SessionDesc session_desc{};
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;

    Slang::ComPtr<slang::ISession> session;
    {
        const SlangResult res = global_session->createSession(session_desc, session.writeRef());
        if (SLANG_FAILED(res)) {
            std::fputs("slang-smoke: failed to create session\n", stderr);
            return 1;
        }
    }

    // 3. Compile both shaders.
    const bool good_ok = compile_shader(session.get(),
                                        "good_shader",
                                        "ps_main",
                                        k_good_source,
                                        /* expect_success = */ true);

    const bool bad_ok = compile_shader(session.get(),
                                       "bad_shader",
                                       "ps_bad",
                                       k_bad_source,
                                       /* expect_success = */ false);

    if (!good_ok) {
        std::fputs("slang-smoke: FAIL – good shader did not compile\n", stderr);
    }
    if (!bad_ok) {
        std::fputs("slang-smoke: FAIL – bad shader did not fail as expected\n", stderr);
    }

    return (good_ok && bad_ok) ? 0 : 1;
}
