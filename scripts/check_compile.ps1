# check_compile.ps1 - compile one or more frontend sources for errors only, without MSBuild.
#
# A quick loop for a file at a time: clang-cl -fsyntax-only with the js frontend's include paths,
# defines and language level, writing no objects. It does not replace a real build - it links
# nothing and does not know about project membership - but it answers "does this file compile" in
# seconds, and several can run side by side without fighting over one project's intermediates.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check_compile.ps1 src\frontends\runtime\api\classes\game\JSUnit.cpp [...]
#
# A header is checked by naming it: it is compiled as a translation unit of its own.

[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files,
    # Win32 (default) or x64.
    [string]$Platform = 'Win32'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[17.0,18.0)' -products * -property installationPath | Select-Object -First 1
$clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
if (-not (Test-Path $clang)) { throw "no clang-cl under $vs" }

[xml]$props = Get-Content (Join-Path $root 'Directory.Build.props')
$unibindVersion = $props.SelectSingleNode('//UnibindVersion').InnerText.Trim()
$arch = if ($Platform -eq 'x64') { 'x64' } else { 'x86' }
$triplet = "$arch-windows-static"
$unibind = Join-Path $root "dependencies\unibind\$unibindVersion\$arch-release\include"
if (-not (Test-Path $unibind)) { throw "unibind $unibindVersion is not unpacked at $unibind - build once to fetch it" }
$vcpkg = Join-Path $root "vcpkg_installed\$triplet\$triplet\include"

$clArgs = @(
    '/nologo', '-fsyntax-only', '/std:c++latest', '/EHsc', '/MT', '/Zc:__cplusplus', '/utf-8',
    '/DWIN32', '/DNDEBUG', '/DNOMINMAX', '/DSPDLOG_WCHAR_TO_UTF8_SUPPORT', '/DSPDLOG_FMT_EXTERNAL',
    '/DD2BS_PROFILING', '/DUNICODE', '/D_UNICODE', '-Wno-pragma-once-outside-header',
    '/I', (Join-Path $root 'src\frontends\runtime'),
    '/I', (Join-Path $root 'src\contract'),
    '/I', (Join-Path $root 'src\core'),
    '/I', (Join-Path $root 'src\services'),
    '/I', (Join-Path $root 'src'),
    '/external:I', $unibind, '/external:I', $vcpkg, '/external:W0'
)
if ($Platform -ne 'x64') { $clArgs += '-m32' }

$failed = 0
foreach ($file in $Files) {
    $path = if ([System.IO.Path]::IsPathRooted($file)) { $file } else { Join-Path $root $file }
    $lang = if ($path -like '*.h') { '/TP' } else { '' }
    # clang writes diagnostics to stderr, which under 'Stop' would end the script at the first one.
    $ErrorActionPreference = 'Continue'
    $out = & $clang @clArgs $lang $path 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) {
        $failed++
        Write-Output "== FAIL $file"
        $out | Select-String -Pattern 'error' | Select-Object -First 40 | ForEach-Object { $_.Line }
    } else {
        Write-Output "== ok   $file"
    }
}
exit $failed
