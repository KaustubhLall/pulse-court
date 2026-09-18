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

.PARAMETER Replay
    Path to a .pulse replay file: launches the historical visualizer
    (Space pause, Right step, +/- speed, R restart, Esc quit).

.PARAMETER ChallengePreviewBundle
    Path to a NON-CERTIFIED dev .pzpb bundle (from pulse_dev_bundle.exe):
    launches the 20-game challenge in preview mode. Requires
    -ChallengeLogDir pointing at a fresh directory.

.PARAMETER ChallengeLogDir
    Fresh log directory for the challenge session (append-only evidence).

.PARAMETER D2D
    Launch with the Direct2D spike flag (--d2d-spike). Short-circuits
    render() into drawing one sprite via Direct2D instead of the normal GDI
    path; see docs/d2d-migration-plan.md and AGENTS.md for status. Combine
    with -Monitor/-Passive as needed.
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
    [string]$Right = $null,

    [Parameter()]
    [switch]$D2D,

    [Parameter()]
    [string]$Replay = $null,

    [Parameter()]
    [string]$ChallengePreviewBundle = $null,

    [Parameter()]
    [string]$ChallengeLogDir = $null
)

$ErrorActionPreference = 'Stop'

# Start-Process -ArgumentList joins an array into one command-line string on
# Windows PowerShell 5.1. Quote every token using the CommandLineToArgvW
# backslash/quote rules so paths with spaces, embedded quotes, or trailing
# backslashes remain one argument after the viewer parses its command line.
function ConvertTo-StartProcessArgument {
    param(
        [AllowNull()]
        [AllowEmptyString()]
        [string]$Value
    )

    if ($null -eq $Value) {
        return '""'
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashCount = 0

    for ($i = 0; $i -lt $Value.Length; $i++) {
        $character = $Value[$i]
        if ($character -eq '\') {
            $backslashCount++
            continue
        }

        if ($character -eq '"') {
            # Backslashes before a quote must be doubled, then one more
            # backslash escapes the quote itself.
            for ($j = 0; $j -lt (($backslashCount * 2) + 1); $j++) {
                [void]$builder.Append('\')
            }
            [void]$builder.Append('"')
            $backslashCount = 0
            continue
        }

        for ($j = 0; $j -lt $backslashCount; $j++) {
            [void]$builder.Append('\')
        }
        [void]$builder.Append($character)
        $backslashCount = 0
    }

    # Backslashes immediately before the closing quote must also be doubled.
    for ($j = 0; $j -lt ($backslashCount * 2); $j++) {
        [void]$builder.Append('\')
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-LauncherAbsolutePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    # Start-Process changes the child working directory to the build output.
    # Resolve caller-relative paths first so -Replay, the preview bundle, and
    # the log directory keep the meaning they had at the PowerShell prompt.
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath(
        (Join-Path (Get-Location).Path $Path)
    )
}

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
if ($Replay) {
    $argList += '--replay'
    $argList += (ConvertTo-LauncherAbsolutePath $Replay)
} elseif ($ChallengePreviewBundle) {
    if (-not $ChallengeLogDir) {
        Write-Error "-ChallengePreviewBundle requires -ChallengeLogDir (must be a fresh directory)"
        exit 1
    }
    $argList += @('--challenge', '--policy-bundle-preview',
                  (ConvertTo-LauncherAbsolutePath $ChallengePreviewBundle),
                  '--challenge-games', '20', '--challenge-log-dir',
                  (ConvertTo-LauncherAbsolutePath $ChallengeLogDir))
} elseif ($Left -and $Right) {
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

if ($D2D) {
    $argList += '--d2d-spike'
}

Write-Host "Launching Pulse Court..." -ForegroundColor Green
$processArgList = @($argList | ForEach-Object {
    ConvertTo-StartProcessArgument $_
})
Write-Host "  $exe $($processArgList -join ' ')" -ForegroundColor Gray

if ($Passive) {
    Write-Host "  (passive mode is observer-only; the window will not activate)" -ForegroundColor Gray
}

$proc = Start-Process -FilePath $exe -ArgumentList $processArgList -WorkingDirectory $baseDir -PassThru -NoNewWindow

# In passive/monitor mode the process should remain open. If it exits quickly,
# a likely cause is an unmatched monitor name; surface that to the caller.
Start-Sleep -Seconds 1
if ($proc.HasExited -and $proc.ExitCode -ne 0) {
    Write-Error "Viewer exited with code $($proc.ExitCode). If -Monitor was used, check the display name."
    exit $proc.ExitCode
}
