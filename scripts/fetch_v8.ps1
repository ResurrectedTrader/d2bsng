# fetch_v8.ps1 - unpack the published V8 archive this build compiles and links against.
#
# Called by the FetchV8 target in Directory.Build.props, which owns the
# repository / tag / asset name and passes them in, so the version this build
# wants is declared in exactly one place. Downloading it here rather than in a
# CI step or a README instruction is what lets a fresh clone build unattended.
#
# One archive carries both the headers and the monolith, and both are installed
# into the same versioned, per-flavor directory - so whatever the compiler
# includes and whatever the linker reads came out of one build of V8.
#
# The release is public, so this is an unauthenticated download of a known URL -
# no gh, no token, nothing to configure.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\fetch_v8.ps1 `
#       -Repo owner/name -Tag v8-X.Y.Z -Asset v8-X.Y.Z-x86-release-msvcA.B.zip `
#       -Destination C:\...\dependencies\v8\X.Y.Z\x86-release

[CmdletBinding()]
param(
    # GitHub repository publishing the archive, as owner/name.
    [Parameter(Mandatory = $true)][string]$Repo,
    # Release tag holding the asset.
    [Parameter(Mandatory = $true)][string]$Tag,
    # Asset file name within that release.
    [Parameter(Mandatory = $true)][string]$Asset,
    # Directory the archive is unpacked into: it ends up holding include\ and
    # v8_monolith.lib.
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'

$libPath = Join-Path $Destination 'v8_monolith.lib'
$incPath = Join-Path $Destination 'include'
# v8.h rather than the directory: an interrupted install could leave a partial
# include\, and a directory that exists is not a directory that is usable.
$incMarker = Join-Path $incPath 'v8.h'

function Test-Installed { (Test-Path -LiteralPath $libPath) -and (Test-Path -LiteralPath $incMarker) }

# Present already - a hand-unpacked archive, a previous build, or a CI cache
# restore. Never re-download: this is a multi-hundred-megabyte archive and a
# no-op build has to stay a no-op.
if (Test-Installed) { exit 0 }

# js and lod114d build concurrently (MSBuild -m) and both compile V8 headers, so
# without this they would race into two downloads of the same archive and then
# fight over the same destination. The loser of the race waits, re-checks, and
# finds the work already done.
$mutexName = 'Global\d2bsng-fetch-v8-' + ($Destination.ToLowerInvariant() -replace '[^a-z0-9]', '-')
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
try { $null = $mutex.WaitOne() } catch [System.Threading.AbandonedMutexException] { }

try {
    if (Test-Installed) { exit 0 }

    $url = "https://github.com/$Repo/releases/download/$Tag/$Asset"

    # Stage in a sibling of the destination, so the final Move-Item calls are
    # renames on the same volume rather than cross-volume copies. A download or
    # extraction that dies partway leaves its wreckage in here, under a name no
    # build looks at, instead of a truncated .lib or a half-written include\
    # that the next build would accept as present.
    $work = Join-Path (Split-Path -Parent $Destination) ".fetch-$PID"

    Write-Host "V8: $Asset not installed at $Destination - fetching from $Repo $Tag."
    Write-Host "V8: one-time download of a few hundred MB, expanding to over a gigabyte. Subsequent builds reuse it."

    try {
        if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $work | Out-Null

        $zip = Join-Path $work $Asset
        $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
        if (Test-Path -LiteralPath $curl) {
            # -f so an HTTP error page is a failure rather than a "successfully"
            # downloaded few hundred bytes of HTML; -L to follow the redirect to
            # the release CDN; -sS to keep the carriage-returning progress meter
            # out of the MSBuild log while still reporting errors.
            & $curl -fL -sS --retry 3 -o $zip $url
            if ($LASTEXITCODE -ne 0) { throw "curl failed ($LASTEXITCODE) downloading $url" }
        } else {
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        }

        # ExtractToDirectory rather than Expand-Archive: far faster on an archive
        # this size, and it validates every entry's CRC, so a truncated or
        # corrupt download throws here instead of producing a half-written
        # library or header tree.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $pkg = Join-Path $work 'pkg'
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $pkg)
        Remove-Item -LiteralPath $zip -Force

        $stagedLib = Join-Path $pkg 'v8_monolith.lib'
        $stagedInc = Join-Path $pkg 'include'
        if (-not (Test-Path -LiteralPath $stagedLib)) { throw "$Asset does not contain v8_monolith.lib" }
        if (-not (Test-Path -LiteralPath (Join-Path $stagedInc 'v8.h'))) { throw "$Asset does not contain include/v8.h" }

        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        if (-not (Test-Path -LiteralPath $incMarker)) {
            if (Test-Path -LiteralPath $incPath) { Remove-Item -LiteralPath $incPath -Recurse -Force }
            Move-Item -LiteralPath $stagedInc -Destination $incPath
        }
        if (-not (Test-Path -LiteralPath $libPath)) {
            Move-Item -LiteralPath $stagedLib -Destination $libPath
        }

        # The archive records the MSVC toolset it was built with. That is a
        # floor, not a match - an older toolset fails the link on undefined
        # __std_* symbols - so surface it here rather than leaving that to be
        # diagnosed at link time.
        $toolsetFile = Join-Path $pkg 'toolset.txt'
        $toolset = if (Test-Path -LiteralPath $toolsetFile) { (Get-Content -LiteralPath $toolsetFile -Raw).Trim() } else { '' }
        $sizeMb = [math]::Round((Get-Item -LiteralPath $libPath).Length / 1MB)
        $headers = (Get-ChildItem -Recurse -File -LiteralPath $incPath).Count
        if ($toolset) {
            Write-Host "V8: $Destination ready - $headers headers, v8_monolith.lib $sizeMb MB; needs MSVC $toolset or newer."
        } else {
            Write-Host "V8: $Destination ready - $headers headers, v8_monolith.lib $sizeMb MB."
        }
    } finally {
        if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    }
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
