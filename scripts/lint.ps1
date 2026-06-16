# lint.ps1 - Parallel clang-tidy runner with per-file caching
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 [-Jobs N] [-NoCache]
#
# Cache is stored next to the compile databases (src/*/Release/lint_cache/, tests/framework/Release/lint_cache/).
# A file is re-linted only when its content hash changes, or the global context changes
# (any header modified, .clang-tidy changed, or compile DB regenerated).

param(
    [int]$Jobs = 0,       # 0 = auto-detect (cores - 1)
    [switch]$NoCache       # Skip cache, re-lint everything
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)  # repo root (this script lives in scripts/)

if ($Jobs -eq 0) {
    $Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
}

# --- Find clang-tidy via vswhere ---
$clangTidy = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe" }
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsPath) {
        @('VC\Tools\Llvm\x64\bin\clang-tidy.exe', 'VC\Tools\Llvm\bin\clang-tidy.exe') | ForEach-Object {
            if (-not $clangTidy) {
                $c = Join-Path $vsPath $_
                if (Test-Path $c) { $script:clangTidy = $c }
            }
        }
    }
}
if (-not $clangTidy) { $clangTidy = (Get-Command clang-tidy -ErrorAction SilentlyContinue).Source }
if (-not $clangTidy) { Write-Host "clang-tidy not found." -ForegroundColor Red; exit 1 }

# --- Compile databases ---
$dbD2bs = 'src\lod114d\Release\d2bs.ClangTidy'
$dbFramework = 'src\framework\Release\framework.ClangTidy'
$dbUtils = 'src\utils\Release\utils.ClangTidy'
$dbTests = 'tests\framework\Release\framework_tests.ClangTidy'

# Auto-regenerate if .vcxproj is newer than the compile DB
function Maybe-RegenDb($dbPath, $vcxproj) {
    if (-not (Test-Path $dbPath)) { return $true }
    if (-not (Test-Path $vcxproj)) { return $false }
    return (Get-Item $vcxproj).LastWriteTime -gt (Get-Item $dbPath).LastWriteTime
}

$needRegen = (Maybe-RegenDb $dbD2bs 'src\lod114d\d2bs.vcxproj') -or (Maybe-RegenDb $dbFramework 'src\framework\framework.vcxproj') -or (Maybe-RegenDb $dbUtils 'src\utils\utils.vcxproj') -or (Maybe-RegenDb $dbTests 'tests\framework\framework_tests.vcxproj')
if ($needRegen) {
    Write-Host 'Compile database missing or stale - regenerating...' -ForegroundColor Yellow
    $msbuild = $null
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
        if (Test-Path $candidate) { $msbuild = $candidate }
    }
    if (-not $msbuild) {
        Write-Host "MSBuild not found. Run '.\build.ps1 Release' first." -ForegroundColor Red
        exit 1
    }
    & $msbuild -p:Configuration=Release -p:Platform=Win32 -p:RunCodeAnalysis=true -m -nologo -v:quiet 2>$null
    if (-not (Test-Path $dbD2bs)) {
        Write-Host "Failed to generate compile database." -ForegroundColor Red
        exit 1
    }
    Write-Host "Compile database ready." -ForegroundColor Green
}

# --- Compute global generation hash ---
# Changes to any header or to .clang-tidy invalidate all cached results.
function Get-GlobalGeneration {
    $hasher = [System.Security.Cryptography.SHA256]::Create()

    # Hash .clang-tidy content
    $tidyPath = '.clang-tidy'
    if (Test-Path $tidyPath) {
        $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $tidyPath).Path)
        $null = $hasher.TransformBlock($bytes, 0, $bytes.Length, $null, 0)
    }

    # Hash compile DB mtimes (captures flag/include changes)
    foreach ($db in @($dbD2bs, $dbFramework, $dbUtils, $dbTests)) {
        if (Test-Path $db) {
            $mtime = (Get-Item $db).LastWriteTimeUtc.Ticks.ToString()
            $bytes = [System.Text.Encoding]::UTF8.GetBytes($mtime)
            $null = $hasher.TransformBlock($bytes, 0, $bytes.Length, $null, 0)
        }
    }

    # Hash max mtime of all header files (any header change invalidates everything)
    # Hash max mtime of all header files (any header change invalidates
    # everything). Use Measure-Object so the accumulator stays in this scope -
    # the previous ForEach-Object loop wrote to $script:maxMtime but compared
    # against the function-local $maxMtime, so the hashed value was always 0
    # and header edits never invalidated the cache.
    $headerDirs = @('src')
    if (Test-Path 'tests') { $headerDirs += 'tests' }
    $headerTicks = Get-ChildItem -Recurse $headerDirs -Filter '*.h' |
        ForEach-Object { $_.LastWriteTimeUtc.Ticks }
    $maxMtime = if ($headerTicks) {
        ($headerTicks | Measure-Object -Maximum).Maximum
    } else {
        [long]0
    }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($maxMtime.ToString())
    $null = $hasher.TransformBlock($bytes, 0, $bytes.Length, $null, 0)

    $null = $hasher.TransformFinalBlock(@(), 0, 0)
    return [BitConverter]::ToString($hasher.Hash).Replace('-', '').ToLower()
}

$globalGen = Get-GlobalGeneration

# --- Collect files ---
# Map each source file to the correct compile database based on its location.
$files = @()
$dbD2bsFull = (Resolve-Path $dbD2bs).Path
$dbFrameworkFull = (Resolve-Path $dbFramework).Path
$dbUtilsFull = (Resolve-Path $dbUtils).Path

Get-ChildItem -Recurse 'src\lod114d' -Filter '*.cpp' | ForEach-Object {
    $files += [PSCustomObject]@{ Path = $_.FullName; Db = $dbD2bsFull; CacheDir = 'src\lod114d\Release\lint_cache' }
}
Get-ChildItem -Recurse 'src\framework' -Filter '*.cpp' | ForEach-Object {
    $files += [PSCustomObject]@{ Path = $_.FullName; Db = $dbFrameworkFull; CacheDir = 'src\framework\Release\lint_cache' }
}
Get-ChildItem -Recurse 'src\utils' -Filter '*.cpp' | ForEach-Object {
    $files += [PSCustomObject]@{ Path = $_.FullName; Db = $dbUtilsFull; CacheDir = 'src\utils\Release\lint_cache' }
}
if (Test-Path $dbTests) {
    $dbTestsFull = (Resolve-Path $dbTests).Path
    Get-ChildItem -Recurse 'tests' -Filter '*.cpp' | ForEach-Object {
        $files += [PSCustomObject]@{ Path = $_.FullName; Db = $dbTestsFull; CacheDir = 'tests\framework\Release\lint_cache' }
    }
}

# --- Cache helpers ---
function Get-FileContentHash($filePath) {
    return (Get-FileHash -Algorithm SHA256 -Path $filePath).Hash.ToLower()
}

function Get-CachePath($file) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($file.Path)
    $dir = $file.CacheDir
    return Join-Path $dir ($name + '.json')
}

function Get-CachedResult($file, $fileHash, $globalGen) {
    $cachePath = Get-CachePath $file
    if (-not (Test-Path $cachePath)) { return $null }
    try {
        $cached = Get-Content $cachePath -Raw | ConvertFrom-Json
        if ($cached.globalGen -eq $globalGen -and $cached.fileHash -eq $fileHash) {
            return $cached
        }
    } catch {}
    return $null
}

function Save-CacheResult($file, $fileHash, $globalGen, $exitCode, $errors) {
    $cachePath = Get-CachePath $file
    $dir = Split-Path $cachePath
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    @{
        globalGen = $globalGen
        fileHash  = $fileHash
        exitCode  = $exitCode
        errors    = $errors
    } | ConvertTo-Json -Compress | Set-Content $cachePath -Encoding UTF8
}

# --- Separate cached vs needs-run ---
$total = $files.Count
$toRun = @()
$cachedResults = @()

if ($NoCache) {
    # Re-lint everything. Items still need a FileHash so job launch (line ~255)
    # and Save-CacheResult can read it - $files entries don't carry one, and
    # Set-StrictMode turns the missing-property access into a terminating error.
    $toRun = $files | ForEach-Object {
        [PSCustomObject]@{ Path = $_.Path; Db = $_.Db; CacheDir = $_.CacheDir; FileHash = (Get-FileContentHash $_.Path) }
    }
} else {
    foreach ($f in $files) {
        $fileHash = Get-FileContentHash $f.Path
        $cached = Get-CachedResult $f $fileHash $globalGen
        if ($cached) {
            $cachedResults += [PSCustomObject]@{
                File     = $f.Path
                ExitCode = $cached.exitCode
                Errors   = @($cached.errors)
                Cached   = $true
            }
        } else {
            $toRun += [PSCustomObject]@{ Path = $f.Path; Db = $f.Db; CacheDir = $f.CacheDir; FileHash = $fileHash }
        }
    }
}

$cachedCount = $cachedResults.Count
$runCount = $toRun.Count
Write-Host "clang-tidy: $total files, $cachedCount cached, $runCount to analyze `($Jobs parallel`)"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

# --- Parallel execution (only for cache misses) ---
$tmpDir = Join-Path $env:TEMP "d2bs_lint_$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

$running = [System.Collections.Generic.List[PSCustomObject]]::new()
$freshResults = [System.Collections.ArrayList]::new()
$done = 0
$fileIndex = 0

function Collect-Result($job) {
    $job.Process.WaitForExit()
    $exitCode = $job.Process.ExitCode
    $job.Process.Dispose()

    $output = ''
    if (Test-Path $job.LogPath) {
        $output = [System.IO.File]::ReadAllText($job.LogPath)
        Remove-Item $job.LogPath -Force -ErrorAction SilentlyContinue
    }

    $errors = @(($output -split "`n") |
        Where-Object { $_ -match 'error:' -and $_ -notmatch 'non-user code' -and $_ -notmatch 'MSB' })

    # Update cache
    Save-CacheResult $job.FileInfo $job.FileHash $globalGen $exitCode $errors

    $null = $script:freshResults.Add([PSCustomObject]@{
        File     = $job.FileInfo.Path
        ExitCode = $exitCode
        Errors   = $errors
        Cached   = $false
    })

    $script:done++
    $name = Split-Path $job.FileInfo.Path -Leaf
    $pct = [math]::Round(($script:done / [Math]::Max($runCount, 1)) * 100)
    Write-Host "`r  [$script:done/$runCount] $pct% - $name          " -NoNewline
}

if ($runCount -gt 0) {
    while ($fileIndex -lt $runCount -or $running.Count -gt 0) {
        # Launch up to $Jobs
        while ($fileIndex -lt $runCount -and $running.Count -lt $Jobs) {
            $f = $toRun[$fileIndex]
            $logPath = Join-Path $tmpDir "$fileIndex.log"
            $psi = [System.Diagnostics.ProcessStartInfo]::new()
            $psi.FileName = 'cmd.exe'
            $psi.Arguments = "/c `"`"$clangTidy`" -p `"$($f.Db)`" `"$($f.Path)`" >`"$logPath`" 2>&1`""
            $psi.UseShellExecute = $false
            $psi.CreateNoWindow = $true
            $proc = [System.Diagnostics.Process]::Start($psi)
            $running.Add([PSCustomObject]@{
                Process  = $proc
                FileInfo = $f
                FileHash = $f.FileHash
                LogPath  = $logPath
            })
            $fileIndex++
        }

        # Harvest finished
        $still = [System.Collections.Generic.List[PSCustomObject]]::new()
        foreach ($job in $running) {
            if ($job.Process.HasExited) {
                Collect-Result $job
            } else {
                $still.Add($job)
            }
        }
        $running = $still

        if ($running.Count -ge $Jobs) { Start-Sleep -Milliseconds 200 }
    }
    Write-Host ""
}

$sw.Stop()
Remove-Item -Recurse -Force $tmpDir -ErrorAction SilentlyContinue

# --- Combine results and report ---
$allResults = @($cachedResults) + @($freshResults)
$failedFiles = @($allResults | Where-Object { $_.ExitCode -ne 0 })
$passCount = @($allResults | Where-Object { $_.ExitCode -eq 0 }).Count

Write-Host ""
if ($failedFiles.Count -gt 0) {
    foreach ($f in $failedFiles) {
        $rel = $f.File.Replace($PWD.Path + '\', '').Replace($PWD.Path + '/', '')
        $tag = ''; if ($f.Cached) { $tag = ' (cached)' }
        $header = '=== FAILED: ' + $rel + $tag + ' ==='
        Write-Host $header -ForegroundColor Red
        foreach ($e in $f.Errors) {
            $line = '  ' + $e.TrimStart()
            Write-Host $line -ForegroundColor Yellow
        }
        Write-Host ""
    }
}

$elapsed = $sw.Elapsed.ToString('mm\:ss')
$cacheNote = ''; if ($cachedCount -gt 0) { $cacheNote = ', ' + $cachedCount + ' cached' }
$color = 'Green'; if ($failedFiles.Count -gt 0) { $color = 'Red' }
$summary = 'clang-tidy: ' + $passCount + ' passed, ' + $failedFiles.Count + ' failed [' + $elapsed + $cacheNote + ']'
Write-Host $summary -ForegroundColor $color

Pop-Location
$exitCode = 0; if ($failedFiles.Count -gt 0) { $exitCode = 1 }
exit $exitCode
