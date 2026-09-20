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
    # Directory the archive is installed as: it ends up holding include\ and
    # v8_monolith.lib. It appears in one move, so its existence means a complete
    # install of this version and flavor.
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'

# The directory IS the unit. Everything is extracted elsewhere and renamed into
# place once, so this never sees a half-populated tree - which a check for one
# file inside it could not tell apart from a complete one.
if (Test-Path -LiteralPath $Destination -PathType Container) { exit 0 }

$parent = Split-Path -Parent $Destination
$leaf = Split-Path -Leaf $Destination

# js and lod114d build concurrently (MSBuild -m) and both compile V8 headers.
# The rename below is what makes concurrent installs *correct*; this mutex is
# what makes them *cheap*, by stopping the second one from downloading a few
# hundred megabytes it is only going to throw away.
$mutexName = 'Global\d2bsng-fetch-v8-' + ($Destination.ToLowerInvariant() -replace '[^a-z0-9]', '-')
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
try { $null = $mutex.WaitOne() } catch [System.Threading.AbandonedMutexException] { }

try {
    if (Test-Path -LiteralPath $Destination -PathType Container) { exit 0 }

    New-Item -ItemType Directory -Force -Path $parent | Out-Null

    # A previous run that was killed outright (Ctrl+C, a closed console, a
    # reboot) cannot have run its cleanup, so sweep those leftovers now. Safe
    # under the mutex: no other instance is mid-install for this destination,
    # and staging is never what the build reads anyway.
    Get-ChildItem -LiteralPath $parent -Directory -Filter "$leaf.staging-*" -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }

    $url = "https://github.com/$Repo/releases/download/$Tag/$Asset"
    # Staging sits beside the destination so the final move is a rename on the
    # same volume - atomic - rather than a cross-volume copy. The GUID keeps two
    # concurrent installs from sharing one staging tree.
    $staging = Join-Path $parent ("$leaf.staging-" + [guid]::NewGuid().ToString('N').Substring(0, 12))
    $payload = Join-Path $staging 'payload'

    Write-Host "V8: $Asset not installed at $Destination - fetching from $Repo $Tag."
    Write-Host "V8: one-time download of a few hundred MB, expanding to over a gigabyte. Subsequent builds reuse it."

    try {
        New-Item -ItemType Directory -Force -Path $staging | Out-Null

        $zip = Join-Path $staging $Asset
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
        # corrupt download throws here - while everything is still in staging.
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $payload)
        Remove-Item -LiteralPath $zip -Force

        if (-not (Test-Path -LiteralPath (Join-Path $payload 'v8_monolith.lib'))) {
            throw "$Asset does not contain v8_monolith.lib"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $payload 'include\v8.h'))) {
            throw "$Asset does not contain include/v8.h"
        }

        $toolsetFile = Join-Path $payload 'toolset.txt'
        $toolset = if (Test-Path -LiteralPath $toolsetFile) { (Get-Content -LiteralPath $toolsetFile -Raw).Trim() } else { '' }
        $sizeMb = [math]::Round((Get-Item -LiteralPath (Join-Path $payload 'v8_monolith.lib')).Length / 1MB)
        $headers = (Get-ChildItem -Recurse -File -LiteralPath (Join-Path $payload 'include')).Count

        # The install. Directory.Move, not Move-Item: PowerShell would move the
        # payload *inside* an existing destination, while this fails - which is
        # what lets two concurrent installs sort themselves out. Whoever renames
        # first wins; the other finds the destination complete and drops its copy.
        try {
            [System.IO.Directory]::Move($payload, $Destination)
        } catch [System.IO.IOException] {
            if (Test-Path -LiteralPath $Destination -PathType Container) {
                Write-Host "V8: $Destination was installed concurrently - keeping that copy."
                exit 0
            }
            throw
        }

        # The archive records the MSVC toolset it was built with. That is a
        # floor, not a match - an older toolset fails the link on undefined
        # __std_* symbols - so surface it here rather than leaving that to be
        # diagnosed at link time.
        if ($toolset) {
            Write-Host "V8: $Destination ready - $headers headers, v8_monolith.lib $sizeMb MB; needs MSVC $toolset or newer."
        } else {
            Write-Host "V8: $Destination ready - $headers headers, v8_monolith.lib $sizeMb MB."
        }
    } finally {
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue }
    }
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
