# build.ps1 - build / format / lint / test entry point for d2bsng.
#
# Usage (from any shell):
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 [target]
# or, from a PowerShell prompt in the repo root:
#   .\build.ps1 [target]
#
# Targets:
#   Release | Debug   build the solution (default: Release)
#   format            clang-format all sources in place
#   check-format      verify formatting without modifying (non-zero exit on diffs)
#   lint              clang-tidy analysis (delegates to scripts\lint.ps1)
#   fix               clang-tidy --fix
#   test              build and run the test suite (js_tests.exe)
#
# The actual build is MSBuild over d2bsng.slnx; this script just locates the
# toolchain and dispatches. You can also build directly with MSBuild or in
# Visual Studio without this script.

param(
    [string]$Target = 'Release',
    # Version baked into the DLL (D2BS_VERSION). CI passes the computed release
    # version; local builds fall back to `git describe` (or 0.0.0-dev).
    [string]$Version = ''
)

# Native tools (msbuild, clang-format, clang-tidy) write to stderr in normal
# operation, so do NOT set $ErrorActionPreference = 'Stop' here - in Windows
# PowerShell that turns native stderr into a terminating error. Exit codes are
# checked explicitly via $LASTEXITCODE instead.
Set-Location $PSScriptRoot

$modes = @('format', 'check-format', 'lint', 'fix', 'test')
if ($modes -contains $Target.ToLower()) {
    $mode = $Target.ToLower()
    $config = 'Release'
} else {
    $mode = 'build'
    $config = $Target
}

# Resolve the version baked into the DLL. CI passes -Version explicitly; for a
# local build derive it from the latest vX.Y.Z tag (commits + dirty appended).
# d2bsng versions start at 2.x (legacy d2bs topped out at 1.6.x), so the dev
# default is 2.0.0; --match keeps the non-semver v8-libs tag out of it.
if (-not $Version) {
    $Version = '2.0.0-dev'
    try {
        $desc = & git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*' --dirty 2>$null
        if ($LASTEXITCODE -eq 0 -and $desc) { $Version = ($desc -replace '^v', '').Trim() }
    } catch {}
}
# Numeric major.minor.patch for the DLL VERSIONINFO (FILEVERSION needs integers);
# strip any -dev / -<commits>-g<sha> suffix, then pad to three parts.
$verNum = ($Version -replace '[^0-9.].*$', '')
$verParts = @($verNum -split '\.' | Where-Object { $_ -ne '' })
while ($verParts.Count -lt 3) { $verParts += '0' }

# --- Locate Visual Studio (vswhere) ---
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe" }
$vsInstall = $null
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
}

function Resolve-FirstPath {
    param([string[]]$Candidates, [string]$FallbackCommand)
    foreach ($c in $Candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    if ($FallbackCommand) {
        $cmd = Get-Command $FallbackCommand -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    return $null
}

function Get-LlvmTool {
    param([string]$Exe)
    $candidates = @()
    if ($vsInstall) {
        $candidates += (Join-Path $vsInstall "VC\Tools\Llvm\x64\bin\$Exe")
        $candidates += (Join-Path $vsInstall "VC\Tools\Llvm\bin\$Exe")
    }
    return Resolve-FirstPath $candidates ([System.IO.Path]::GetFileNameWithoutExtension($Exe))
}

# clang-format is version-pinned so CI and local agree. An unpinned, VS-bundled
# clang-format drifts between machines (it infers East vs West pointer alignment
# differently by version, among other things), which makes `check-format` pass
# locally yet fail in CI. CI installs this exact version with
# `pip install clang-format==<ver>` (see .github/workflows/ci.yml); locally we
# prefer a matching clang-format on PATH and fall back to the VS-bundled tool.
$PINNED_CLANG_FORMAT = '20.1.8'

function Get-ClangFormat {
    # Pick the first clang-format on PATH that reports the pinned version (CI
    # installs it via pip). Scan ALL PATH matches, not just the first, so a
    # different-version or non-clang-format `clang-format` ahead on PATH (e.g. a
    # Chromium depot_tools wrapper) can't shadow the pinned one. Probe quietly and
    # fall back to the VS-bundled tool for local dev.
    foreach ($cmd in @(Get-Command clang-format -All -ErrorAction SilentlyContinue)) {
        $verLine = ''
        try { $verLine = (& $cmd.Source --version 2>$null | Out-String) } catch {}
        if ($verLine -match [regex]::Escape($PINNED_CLANG_FORMAT)) { return $cmd.Source }
    }
    return Get-LlvmTool 'clang-format.exe'
}

# --- Resolve MSBuild ---
$msbuildCandidates = @()
if ($vsInstall) {
    $msbuildCandidates += (Join-Path $vsInstall 'MSBuild\Current\Bin\amd64\MSBuild.exe')
    $msbuildCandidates += (Join-Path $vsInstall 'MSBuild\Current\Bin\MSBuild.exe')
}
$msbuild = Resolve-FirstPath $msbuildCandidates 'msbuild'
if (-not $msbuild) {
    Write-Host 'MSBuild not found. Install Visual Studio / Build Tools or put msbuild.exe on PATH.' -ForegroundColor Red
    exit 1
}

# --- Source files for clang-format (skip vcpkg_installed) ---
function Get-SourceFiles {
    $paths = @('src', 'tests') | Where-Object { Test-Path $_ }
    return Get-ChildItem -Path $paths -Recurse -File |
        Where-Object { ($_.Extension -eq '.cpp' -or $_.Extension -eq '.h') -and $_.FullName -notmatch 'vcpkg_installed' }
}

switch ($mode) {
    'format' {
        $clangFormat = Get-ClangFormat
        if (-not $clangFormat) {
            Write-Host "clang-format not found. Install it with 'pip install clang-format==$PINNED_CLANG_FORMAT' or via Visual Studio LLVM tools." -ForegroundColor Red
            exit 1
        }
        Write-Host 'Formatting source files...'
        foreach ($f in Get-SourceFiles) { & $clangFormat -i $f.FullName }
        Write-Host 'Done.'
        exit 0
    }
    'check-format' {
        $clangFormat = Get-ClangFormat
        if (-not $clangFormat) {
            Write-Host "clang-format not found. Install it with 'pip install clang-format==$PINNED_CLANG_FORMAT' or via Visual Studio LLVM tools." -ForegroundColor Red
            exit 1
        }
        Write-Host 'Checking format...'
        $failed = $false
        foreach ($f in Get-SourceFiles) {
            & $clangFormat --dry-run --Werror $f.FullName
            if ($LASTEXITCODE -ne 0) { $failed = $true }
        }
        if ($failed) { exit 1 }
        Write-Host 'All files properly formatted.'
        exit 0
    }
    'lint' {
        & (Join-Path $PSScriptRoot 'scripts\lint.ps1')
        exit $LASTEXITCODE
    }
    'fix' {
        $clangTidy = Get-LlvmTool 'clang-tidy.exe'
        if (-not $clangTidy) {
            Write-Host 'clang-tidy not found. Install LLVM tools via Visual Studio or put clang-tidy.exe on PATH.' -ForegroundColor Red
            exit 1
        }
        $dbByDir = [ordered]@{
            'src\frontends\js'     = 'src\frontends\js\Release\js.ClangTidy'
            'src\backends\lod114d' = 'src\backends\lod114d\Release\lod114d.ClangTidy'
            'src\glue\js-lod114d'  = 'src\glue\js-lod114d\Release\d2bs.ClangTidy'
            'src\contract'         = 'src\contract\Release\contract.ClangTidy'
            'src\core'             = 'src\core\Release\core.ClangTidy'
            'src\utils'            = 'src\utils\Release\utils.ClangTidy'
        }
        if (-not (Test-Path $dbByDir['src\backends\lod114d'])) {
            Write-Host 'Compilation database not found. Run ".\build.ps1 lint" first to generate it.' -ForegroundColor Red
            exit 1
        }
        Write-Host 'Running clang-tidy --fix on source files...'
        foreach ($dir in $dbByDir.Keys) {
            $db = $dbByDir[$dir]
            Get-ChildItem -Path $dir -Recurse -File -Filter '*.cpp' | ForEach-Object {
                Write-Host "Fixing: $($_.FullName)"
                & $clangTidy --fix -p $db $_.FullName
            }
        }
        Write-Host 'Done.'
        exit 0
    }
    'test' {
        & $msbuild -p:Configuration=Release -p:Platform=Win32 -t:js_tests
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $PSScriptRoot 'Release\js_tests.exe')
        exit $LASTEXITCODE
    }
    default {
        & $msbuild -p:Configuration=$config -p:Platform=Win32 -p:D2bsVersion=$Version `
            -p:D2bsVersionMajor=$($verParts[0]) -p:D2bsVersionMinor=$($verParts[1]) -p:D2bsVersionPatch=$($verParts[2])
        exit $LASTEXITCODE
    }
}
