# lint.ps1 - Parallel clang-tidy runner with dependency-aware per-file caching
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\lint.ps1 [-Jobs N] [-NoCache]
#
# Cache is stored next to the compile databases (src/*/Release/lint_cache/, tests/framework/Release/lint_cache/).
# A translation unit is re-linted only when its own content, one of its included
# *project* headers' content, or its compile command changes (clang-scan-deps
# discovers the headers). Toolchain / config / dependency changes invalidate
# everything via a coarse environment token. This is direct-mode caching (ccache
# style): the per-TU header list is cached and reused while the key still matches,
# so clang-scan-deps only runs on a miss.

param(
    [int]$Jobs = 0,       # 0 = auto-detect (cores - 1)
    [switch]$NoCache       # Skip cache, re-lint everything
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)  # repo root (this script lives in scripts/)
$repoRoot = (Get-Location).Path

if ($Jobs -eq 0) {
    $Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
}

# --- Find clang-tidy + clang-scan-deps via vswhere ---
$clangTidy = $null
$scanDeps = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { $vswhere = "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe" }
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsPath) {
        @('VC\Tools\Llvm\x64\bin\clang-tidy.exe', 'VC\Tools\Llvm\bin\clang-tidy.exe') | ForEach-Object {
            if (-not $clangTidy) { $c = Join-Path $vsPath $_; if (Test-Path $c) { $script:clangTidy = $c } }
        }
        @('VC\Tools\Llvm\x64\bin\clang-scan-deps.exe', 'VC\Tools\Llvm\bin\clang-scan-deps.exe') | ForEach-Object {
            if (-not $scanDeps) { $c = Join-Path $vsPath $_; if (Test-Path $c) { $script:scanDeps = $c } }
        }
    }
}
if (-not $clangTidy) { $clangTidy = (Get-Command clang-tidy -ErrorAction SilentlyContinue).Source }
if (-not $clangTidy) { Write-Host "clang-tidy not found." -ForegroundColor Red; exit 1 }
if (-not $scanDeps) { $scanDeps = (Get-Command clang-scan-deps -ErrorAction SilentlyContinue).Source }

# Without clang-scan-deps we can't track per-TU header dependencies, so fall back
# to coarse invalidation: the environment token folds in the max header mtime, so
# any header edit re-lints everything.
$useDepCache = [bool]$scanDeps
if (-not $useDepCache) {
    Write-Host "clang-scan-deps not found - using coarse (whole-header) cache invalidation." -ForegroundColor Yellow
}

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
    # RunCodeAnalysis writes compile_commands.json but also runs a full clang-tidy
    # pass over every TU (~30 min, serial) - work this script immediately redoes in
    # parallel with caching below. The ClangTidy MSBuild task writes the DB *before*
    # invoking the analyzer, so pointing the analyzer at a no-op exe yields the same
    # DB in seconds. D2bsInstallDir= skips the post-build deploy copy that would
    # otherwise fail against a running game.
    $tidyStub = @()
    $noopTidy = Join-Path $env:TEMP 'd2bs_lint_noop_tidy.exe'
    try {
        if (-not (Test-Path $noopTidy)) {
            Add-Type -TypeDefinition 'public class P{public static void Main(){}}' -OutputAssembly $noopTidy -OutputType ConsoleApplication -ErrorAction Stop
        }
        $tidyStub = @("-p:ClangTidyToolPath=$(Split-Path $noopTidy)", "-p:ClangTidyToolExe=$(Split-Path $noopTidy -Leaf)")
    } catch {
        Write-Host "Note: clang-tidy stub unavailable ($($_.Exception.Message)); DB generation will run the full clang-tidy analysis." -ForegroundColor Yellow
    }
    & $msbuild -p:Configuration=Release -p:Platform=Win32 -p:RunCodeAnalysis=true -p:D2bsInstallDir= $tidyStub -m -nologo -v:quiet 2>$null
    if (-not (Test-Path $dbD2bs)) {
        Write-Host "Failed to generate compile database." -ForegroundColor Red
        exit 1
    }
    Write-Host "Compile database ready." -ForegroundColor Green
}

# --- Hashing / path helpers ---
$sha256 = [System.Security.Cryptography.SHA256]::Create()
function Get-FileContentHash($filePath) {
    return ([BitConverter]::ToString($sha256.ComputeHash([System.IO.File]::ReadAllBytes($filePath))).Replace('-', '')).ToLower()
}
function Get-StringHash([string]$s) {
    return ([BitConverter]::ToString($sha256.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($s))).Replace('-', '')).ToLower()
}
function Norm([string]$p) { return ($p -replace '/', '\').ToLower() }
function Get-Prop($obj, $name) {
    if ($obj -and ($obj.PSObject.Properties.Name -contains $name)) { return $obj.$name }
    return $null
}
$srcPrefix = Norm ((Join-Path $repoRoot 'src') + '\')

# Fingerprint a vendored header tree by path+size+mtime (no content reads, ~150ms
# for V8+D2MOO). Catches re-vendoring, submodule re-pin, and dirty working-tree edits.
function Get-TreeFingerprint($dir) {
    if (-not (Test-Path $dir)) { return '' }
    $items = Get-ChildItem -Recurse -File -Filter '*.h' $dir -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        ForEach-Object { "$($_.FullName.ToLower())|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)" }
    return Get-StringHash (($items) -join "`n")
}

# --- Environment token: toolchain / config / third-party deps ---
# Covers everything that can change a TU's analysis but is NOT in its own compile
# command or project headers: the clang-tidy version, .clang-tidy configs, the vcpkg
# manifest, and a metadata fingerprint of the vendored V8 / D2MOO header trees.
# Those trees are excluded from per-TU hashing, so they are tracked here instead - a
# re-vendor, submodule re-pin, or dirty edit rolls the whole cache.
function Get-EnvToken {
    $parts = New-Object System.Collections.Generic.List[string]
    try { $parts.Add(((& $clangTidy --version 2>$null) -join ' ')) } catch { $parts.Add($clangTidy) }
    foreach ($cfg in @('.clang-tidy', 'tests\framework\.clang-tidy', 'tests\framework\pathfinding\reference\.clang-tidy', 'vcpkg.json')) {
        if (Test-Path $cfg) { $parts.Add($cfg + '=' + (Get-FileContentHash (Resolve-Path $cfg).Path)) }
    }
    $parts.Add('v8=' + (Get-TreeFingerprint 'dependencies\v8\include'))
    $parts.Add('d2moo=' + (Get-TreeFingerprint 'dependencies\D2MOO\source'))
    if (-not $useDepCache) {
        $dirs = @('src'); if (Test-Path 'tests') { $dirs += 'tests' }
        $ticks = Get-ChildItem -Recurse $dirs -Filter '*.h' | ForEach-Object { $_.LastWriteTimeUtc.Ticks }
        $max = if ($ticks) { ($ticks | Measure-Object -Maximum).Maximum } else { [long]0 }
        $parts.Add('hdrmax=' + $max)
    }
    return Get-StringHash ($parts -join '|')
}
$envToken = Get-EnvToken

# --- Per-TU cache key ---
# key = hash(envToken + cpp content + each project-header dep content + compile command)
function Get-TuKey($cppPath, $deps, $command) {
    $parts = New-Object System.Collections.Generic.List[string]
    $parts.Add($envToken)
    $parts.Add((Get-FileContentHash $cppPath))
    foreach ($d in (@($deps) | Sort-Object)) {
        $h = 'MISSING'
        try { if (Test-Path -LiteralPath $d) { $h = Get-FileContentHash $d } } catch {}
        $parts.Add((Norm $d) + '=' + $h)
    }
    $parts.Add('cmd=' + $command)
    return Get-StringHash ($parts -join '|')
}

function Get-CachePath($file) {
    # Suffix with a short hash of the full path so same-named sources in one project
    # (lod114d has console/Console.cpp and game/Console.cpp) don't share a cache file.
    $name = [System.IO.Path]::GetFileNameWithoutExtension($file.Path)
    $tag = (Get-StringHash (Norm $file.Path)).Substring(0, 8)
    return Join-Path $file.CacheDir ($name + '_' + $tag + '.json')
}
function Save-CacheResult($file, $key, $deps, $exitCode, $errors) {
    $cachePath = Get-CachePath $file
    $dir = Split-Path $cachePath
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    @{
        envToken = $envToken
        key      = $key
        deps     = @($deps)
        exitCode = $exitCode
        errors   = @($errors)
    } | ConvertTo-Json -Compress -Depth 5 | Set-Content $cachePath -Encoding UTF8
}

# Load file -> compile-command entry from every DB (for keys + dependency scanning).
function Load-DbCommands($dbDirs) {
    $map = @{}
    foreach ($dbDir in $dbDirs) {
        $cc = Join-Path $dbDir 'compile_commands.json'
        if (-not (Test-Path $cc)) { continue }
        foreach ($e in (Get-Content $cc -Raw | ConvertFrom-Json)) {
            $f = $e.file
            if (-not [System.IO.Path]::IsPathRooted($f)) { $f = Join-Path $e.directory $f }
            $map[(Norm $f)] = $e
        }
    }
    return $map
}

# Run clang-scan-deps once over the given entries; return file(lower) -> @(project headers).
function Get-DepsForFiles($scanEntries, $scratchDir) {
    $map = @{}
    if (-not $useDepCache -or @($scanEntries).Count -eq 0) { return $map }
    if (-not (Test-Path $scratchDir)) { New-Item -ItemType Directory -Path $scratchDir -Force | Out-Null }
    $cc = Join-Path $scratchDir 'compile_commands.json'
    $json = '[' + ((@($scanEntries) | ForEach-Object { $_ | ConvertTo-Json -Depth 6 }) -join ',') + ']'
    [System.IO.File]::WriteAllText($cc, $json)
    $make = & $scanDeps --format=make --compilation-database=$cc 2>$null
    # make output: one rule per TU ("target: src hdr hdr ..."). Join continuations,
    # then attribute each rule's project headers to its .cpp source.
    $text = ($make -join "`n") -replace '\\\r?\n', ' '
    foreach ($rule in ($text -split "`r?`n")) {
        if ($rule -notmatch '\S') { continue }
        $tokens = $rule.Trim() -split '\s+'
        $src = $tokens | Where-Object { $_ -match '\.cpp$' } | Select-Object -First 1
        if (-not $src) { continue }
        $hdrs = $tokens |
            Where-Object { (Norm $_).StartsWith($srcPrefix) -and ($_ -match '\.(h|hpp|hxx|inc)$') } |
            ForEach-Object { ($_ -replace '/', '\') } | Sort-Object -Unique
        $map[(Norm $src)] = @($hdrs)
    }
    return $map
}

# --- Collect files ---
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

$cmdMap = Load-DbCommands @($dbD2bs, $dbFramework, $dbUtils, $dbTests)
$tmpDir = Join-Path $env:TEMP "d2bs_lint_$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

# --- Separate cached (direct-mode hit) vs needs-run ---
$total = $files.Count
$toRun = @()
$cachedResults = @()

foreach ($f in $files) {
    $entry = $cmdMap[(Norm $f.Path)]
    $cmd = if ($entry) { $entry.command } else { '' }
    $hit = $false
    if (-not $NoCache) {
        $cachePath = Get-CachePath $f
        if (Test-Path $cachePath) {
            $cached = $null
            try { $cached = Get-Content $cachePath -Raw | ConvertFrom-Json } catch {}
            if ((Get-Prop $cached 'envToken') -eq $envToken -and (Get-Prop $cached 'key')) {
                # Recompute the key from the cached dep list; a match proves the .cpp,
                # all recorded deps, and the command are unchanged (direct mode).
                $cand = Get-TuKey $f.Path @(Get-Prop $cached 'deps') $cmd
                if ($cand -eq (Get-Prop $cached 'key')) {
                    $cachedResults += [PSCustomObject]@{ File = $f.Path; ExitCode = (Get-Prop $cached 'exitCode'); Errors = @(Get-Prop $cached 'errors'); Cached = $true }
                    $hit = $true
                }
            }
        }
    }
    if (-not $hit) {
        $toRun += [PSCustomObject]@{ Path = $f.Path; Db = $f.Db; CacheDir = $f.CacheDir; Cmd = $cmd; Entry = $entry }
    }
}

$cachedCount = $cachedResults.Count
$runCount = $toRun.Count

# --- Refresh dependencies for the misses (single clang-scan-deps pass) ---
$freshDeps = @{}
if ($runCount -gt 0 -and $useDepCache) {
    Write-Host "scanning dependencies for $runCount file(s)..." -ForegroundColor DarkGray
    $scanEntries = @()
    foreach ($r in $toRun) {
        if ($r.Entry) {
            $scanEntries += [PSCustomObject]@{ directory = $r.Entry.directory; command = $r.Entry.command; file = $r.Entry.file }
        }
    }
    $freshDeps = Get-DepsForFiles $scanEntries (Join-Path $tmpDir 'scandb')
}

Write-Host "clang-tidy: $total files, $cachedCount cached, $runCount to analyze `($Jobs parallel`)"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

# --- Parallel execution (only for cache misses) ---
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

    # Cache the result keyed on this TU's content + its project-header deps + command.
    # Skip caching only when dependency scanning was expected but produced nothing for
    # this TU (scan failure) - caching empty deps then would miss real header changes.
    $norm = Norm $job.FileInfo.Path
    $scanned = (-not $useDepCache) -or $script:freshDeps.ContainsKey($norm)
    if ($scanned) {
        $deps = if ($script:freshDeps.ContainsKey($norm)) { @($script:freshDeps[$norm]) } else { @() }
        $key = Get-TuKey $job.FileInfo.Path $deps $job.FileInfo.Cmd
        Save-CacheResult $job.FileInfo $key $deps $exitCode $errors
    }

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
