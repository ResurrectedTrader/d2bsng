# fetch_v8.ps1 - obtain the prebuilt V8 monolith this build links against.
#
# Called by the FetchV8Monolith target in Directory.Build.props, which owns the
# repository / tag / asset name and passes them in, so the version this build
# wants is declared in exactly one place. Downloading it here rather than in a
# CI step or a README instruction is what lets a fresh clone build unattended.
#
# The release is public, so this is an unauthenticated download of a known URL -
# no gh, no token, nothing to configure.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\fetch_v8.ps1 `
#       -Repo owner/name -Tag v8-X.Y.Z -Asset v8-X.Y.Z-x86-release-msvcA.B.zip `
#       -Destination C:\...\dependencies\v8\libs\x86-release\v8_monolith.lib

[CmdletBinding()]
param(
    # GitHub repository publishing the monolith, as owner/name.
    [Parameter(Mandatory = $true)][string]$Repo,
    # Release tag holding the asset.
    [Parameter(Mandatory = $true)][string]$Tag,
    # Asset file name within that release.
    [Parameter(Mandatory = $true)][string]$Asset,
    # Full path of the v8_monolith.lib this configuration links.
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'

# Present already - a hand-placed library, a previous build, or a CI cache
# restore. Never re-download: this is a multi-hundred-megabyte archive and a
# no-op build has to stay a no-op.
if (Test-Path -LiteralPath $Destination) { exit 0 }

$libName = Split-Path -Leaf $Destination
$destDir = Split-Path -Parent $Destination
$url = "https://github.com/$Repo/releases/download/$Tag/$Asset"

# Stage in a sibling of the destination, so the final Move-Item is a rename on
# the same volume rather than a cross-volume copy. A download or extraction that
# dies partway leaves its wreckage in here, under a name the build never links,
# instead of a truncated .lib that the next build would accept as present.
$work = Join-Path $destDir ".fetch-$PID"

Write-Host "V8: $libName not found - fetching $Asset from $Repo $Tag."
Write-Host "V8: one-time download of a few hundred MB, expanding to over a gigabyte. Subsequent builds reuse it."

try {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $work | Out-Null

    $zip = Join-Path $work $Asset
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    if (Test-Path -LiteralPath $curl) {
        # -f so an HTTP error page is a failure rather than a "successfully"
        # downloaded few hundred bytes of HTML; -L to follow the redirect to the
        # release CDN; -sS to keep the carriage-returning progress meter out of
        # the MSBuild log while still reporting errors.
        & $curl -fL -sS --retry 3 -o $zip $url
        if ($LASTEXITCODE -ne 0) { throw "curl failed ($LASTEXITCODE) downloading $url" }
    } else {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    }

    # ExtractToDirectory rather than Expand-Archive: far faster on an archive
    # this size, and it validates every entry's CRC, so a truncated or corrupt
    # download throws here instead of producing a half-written library.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $pkg = Join-Path $work 'pkg'
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $pkg)
    Remove-Item -LiteralPath $zip -Force

    $lib = Join-Path $pkg $libName
    if (-not (Test-Path -LiteralPath $lib)) { throw "$Asset does not contain $libName" }

    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Move-Item -LiteralPath $lib -Destination $Destination -Force

    # The archive records the MSVC toolset it was built with. That is a floor,
    # not a match - an older toolset fails the link on undefined __std_* symbols
    # - so surface it here rather than leaving that to be diagnosed at link time.
    $toolsetFile = Join-Path $pkg 'toolset.txt'
    $toolset = if (Test-Path -LiteralPath $toolsetFile) { (Get-Content -LiteralPath $toolsetFile -Raw).Trim() } else { '' }
    $sizeMb = [math]::Round((Get-Item -LiteralPath $Destination).Length / 1MB)
    if ($toolset) {
        Write-Host "V8: $Destination ready ($sizeMb MB); needs MSVC $toolset or newer."
    } else {
        Write-Host "V8: $Destination ready ($sizeMb MB)."
    }
} finally {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
}
