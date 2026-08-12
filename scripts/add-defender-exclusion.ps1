<#
.SYNOPSIS
    Add (or remove) a Microsoft Defender exclusion for the qvim install directory.

.DESCRIPTION
    On Windows, the dominant cost of a *cold* qvim launch (first run after a build or
    update — 10-20s) is Defender's real-time "first-sight" scan of qvim.exe and every Qt
    DLL it loads. Defender caches its verdict by file content hash, so the cost is paid
    once per unique build and then launches are fast (~1s) — until the next rebuild
    produces new bytes and the scan happens all over again.

    Excluding the install directory from real-time scanning collapses cold launches from
    ~8s to ~1.5s and keeps rebuilds fast. This is the single most effective cold-start fix.

    SECURITY TRADEOFF: an excluded path is no longer scanned by Defender's real-time
    protection. Only exclude a directory whose contents you trust and control (your own
    build output). Do NOT exclude a broad location such as your whole user profile or a
    downloads folder. This script defaults to the directory containing the built qvim.exe.

.PARAMETER Path
    Directory to exclude. Defaults to the folder containing the release qvim.exe next to
    this script's repo (build\release\RelWithDebInfo).

.PARAMETER Remove
    Remove the exclusion instead of adding it.

.EXAMPLE
    # Add an exclusion for the default release output (run from an elevated shell):
    pwsh -NoProfile -File scripts\add-defender-exclusion.ps1

.EXAMPLE
    # Exclude a specific install directory:
    pwsh -NoProfile -File scripts\add-defender-exclusion.ps1 -Path 'C:\Tools\qvim'

.EXAMPLE
    # Undo:
    pwsh -NoProfile -File scripts\add-defender-exclusion.ps1 -Remove
#>
[CmdletBinding()]
param(
    [string]$Path,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"

# Resolve default path: build\release\RelWithDebInfo relative to the repo root (parent of scripts\).
if (-not $Path) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $Path = Join-Path $repoRoot "build\release\RelWithDebInfo"
}

# Normalise to a full path. The directory need not exist yet (you may exclude before first build),
# but warn if it does not so a typo is obvious.
try {
    $Path = [System.IO.Path]::GetFullPath($Path)
} catch {
    throw "Invalid path: $Path"
}
if (-not (Test-Path -LiteralPath $Path)) {
    Write-Warning "Path does not exist yet: $Path (excluding it anyway)."
}

# Require elevation — Set-MpPreference needs admin.
$isAdmin = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    throw "This script must be run from an elevated (Administrator) PowerShell. " +
          "Right-click PowerShell -> 'Run as administrator', then re-run."
}

# Confirm Defender cmdlets are available (they are absent if a third-party AV replaced Defender).
if (-not (Get-Command Get-MpPreference -ErrorAction SilentlyContinue)) {
    throw "Microsoft Defender cmdlets are not available on this machine. " +
          "If a third-party antivirus is active, add the exclusion in its own settings instead."
}

$existing = @((Get-MpPreference).ExclusionPath)
$already = $existing -contains $Path

if ($Remove) {
    if (-not $already) {
        Write-Host "No exclusion found for: $Path (nothing to remove)."
        return
    }
    Remove-MpPreference -ExclusionPath $Path
    Write-Host "Removed Defender exclusion: $Path" -ForegroundColor Green
    return
}

if ($already) {
    Write-Host "Defender exclusion already present: $Path" -ForegroundColor Yellow
    return
}

Add-MpPreference -ExclusionPath $Path
Write-Host "Added Defender exclusion: $Path" -ForegroundColor Green
Write-Host ""
Write-Host "Cold launches of qvim from this directory will now skip Defender's real-time scan."
Write-Host "To undo:  pwsh -NoProfile -File scripts\add-defender-exclusion.ps1 -Remove -Path '$Path'"
