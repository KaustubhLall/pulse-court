<#
.SYNOPSIS
    Canonical Pulse Court viewer launcher.

.DESCRIPTION
    Validates the canonical Release build path and launches pulse_viewer.exe.
    Defaults to --setup. Pass -Monitor and -Passive for an observer-only test
    window on a specific display. -Left and -Right are advanced direct-match
    options for automated smoke use; they are not the normal human entry point.

    -Passive is observer-only: the viewer window does not activate or steal
    focus. It is intended for visual QA and screenshot capture, not for normal
    interactive play.

.PARAMETER Monitor
    Friendly display name to target (e.g. "scepter"). Matched case-insensitively.

.PARAMETER Passive
    Launch in passive observer mode (no-activate). Explicitly observer-only.

.PARAMETER Left
    Direct character selection for player 1 (kite, vale, bastion).

.PARAMETER Right
    Direct character selection for player 2 (kite, vale, bastion).
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string]$Monitor = $null,

    [Parameter()]
    [switch]$Passive,

    [Parameter()]
    [ValidateSet("kite", "vale", "bastion", "Kite", "Vale", "Bastion")]
    [string]$Left = $null,

    [Parameter()]
    [ValidateSet("kite", "vale", "bastion", "Kite", "Vale", "Bastion")]
    [string]$Right = $null
)

$ErrorActionPreference = 'Stop'

$repoRoot = $PSScriptRoot
if ($repoRoot -and (Test-Path (Join-Path $repoRoot '..' -Resolve))) {
    $repoRoot = (Resolve-Path (Join-Path $repoRoot '..')).Path
} else {
    $repoRoot = (Get-Location).Path
}

$baseDir = Join-Path $repoRoot 'build\release\Release'
$exe = Join-Path $baseDir 'pulse_viewer.exe'
$assets = Join-Path $baseDir 'assets'

if (-not (Test-Path $exe -PathType Leaf)) {
    Write-Error "Viewer executable not found: $exe"
    exit 1
}
if (-not (Test-Path $assets -PathType Container)) {
    Write-Error "Assets directory not found: $assets"
    exit 1
}

$argList = @()
if ($Left -and $Right) {
    $argList += '--left'
    $argList += $Left.ToLower()
    $argList += '--right'
    $argList += $Right.ToLower()
} else {
    $argList += '--setup'
}

if ($Monitor) {
    $argList += '--monitor'
    $argList += $Monitor
}

if ($Passive) {
    $argList += '--passive'
}

Write-Host "Launching Pulse Court..." -ForegroundColor Green
Write-Host "  $exe $argList" -ForegroundColor Gray

if ($Passive) {
    Write-Host "  (passive mode is observer-only; the window will not activate)" -ForegroundColor Gray
}

$proc = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $baseDir -PassThru -NoNewWindow

# In passive/monitor mode the process should remain open. If it exits quickly,
# a likely cause is an unmatched monitor name; surface that to the caller.
Start-Sleep -Seconds 1
if ($proc.HasExited -and $proc.ExitCode -ne 0) {
    Write-Error "Viewer exited with code $($proc.ExitCode). If -Monitor was used, check the display name."
    exit $proc.ExitCode
}
