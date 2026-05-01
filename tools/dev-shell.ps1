# tools/dev-shell.ps1 — one-shot VS DevShell + Slang/Ninja PATH setup.
#
# Source from any PowerShell session before invoking cmake / ninja / cl /
# ctest / clang-format. Idempotent — re-running does nothing harmful.
#
# Usage:
#     . .\tools\dev-shell.ps1            # dot-source: stays in current shell
#     pwsh -File .\tools\dev-shell.ps1   # spawn-and-exit: env is lost
#                                          (use dot-source for builds)
#
# After dot-sourcing you can run:
#     cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#     cmake --build build
#     ctest --test-dir build --output-on-failure
#
# Caching: idempotency is enforced via $env:HLSL_CLIPPY_DEV_SHELL_READY.
# To force a re-init (e.g. after a VS update) clear it first:
#     Remove-Item Env:HLSL_CLIPPY_DEV_SHELL_READY
#     . .\tools\dev-shell.ps1

if ($env:HLSL_CLIPPY_DEV_SHELL_READY -eq "1") {
    Write-Host "dev-shell: already initialised in this session (set HLSL_CLIPPY_DEV_SHELL_READY='' to redo)"
    return
}

# ── 1. Locate Visual Studio ────────────────────────────────────────────────
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "dev-shell: vswhere.exe not found at $vswhere"
    return
}
$VS = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $VS) {
    Write-Error "dev-shell: no VS installation with the C++ workload found"
    return
}
Write-Host "dev-shell: VS install = $VS"

# ── 2. Enter the VS Dev Shell (cl.exe / link.exe / INCLUDE / LIB on PATH) ──
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:Path"
$DevShellModule = Join-Path $VS "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path $DevShellModule)) {
    Write-Error "dev-shell: Microsoft.VisualStudio.DevShell.dll not at $DevShellModule"
    return
}
Import-Module $DevShellModule
Enter-VsDevShell -VsInstallPath $VS -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64 -no_logo" | Out-Null

# ── 3. Add VS-bundled Ninja + CMake to PATH ────────────────────────────────
$ninjaDir = Join-Path $VS "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$cmakeDir = Join-Path $VS "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$env:Path = "$ninjaDir;$cmakeDir;$env:Path"

# ── 4. Add the Slang prebuilt cache's bin/ to PATH so test exes resolve   ──
#       slang.dll + the 6 transitive runtime DLLs at runtime without needing
#       a per-target POST_BUILD copy. Phase 5 v0.6 follow-up: replace this
#       with a hlsl_clippy_deploy_slang_dlls(target) helper in UseSlang.cmake
#       so the deployed bits ship to the build/ tree directly.
$slangVer = $null
$slangVersionFile = "cmake\SlangVersion.cmake"
if (Test-Path $slangVersionFile) {
    $line = Select-String -Path $slangVersionFile -Pattern 'HLSL_CLIPPY_SLANG_VERSION\s+"([^"]+)"' | Select-Object -First 1
    if ($line) { $slangVer = $line.Matches[0].Groups[1].Value }
}
if ($slangVer) {
    $slangBin = Join-Path $env:LOCALAPPDATA "hlsl-clippy\slang\$slangVer\bin"
    if (Test-Path $slangBin) {
        $env:Path = "$slangBin;$env:Path"
        Write-Host "dev-shell: Slang $slangVer bin on PATH ($slangBin)"
    } else {
        Write-Host "dev-shell: Slang cache empty for $slangVer; run tools\fetch-slang.ps1"
    }
} else {
    Write-Host "dev-shell: cmake\SlangVersion.cmake not found; PATH not extended for Slang"
}

# ── 5. Quick sanity-check ──────────────────────────────────────────────────
foreach ($t in @("cmake", "ninja", "cl", "link")) {
    $p = Get-Command $t -ErrorAction SilentlyContinue
    if ($p) { Write-Host "dev-shell:   $t -> $($p.Source)" }
    else    { Write-Warning "dev-shell:   $t NOT FOUND on PATH" }
}

$env:HLSL_CLIPPY_DEV_SHELL_READY = "1"
Write-Host "dev-shell: ready. Now run cmake / ninja / ctest / clang-format directly."
