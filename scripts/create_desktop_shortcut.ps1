<#
.SYNOPSIS
    Creates a Pulse Court desktop shortcut pointing to pulse_viewer.exe --setup.

.DESCRIPTION
    Validates that the viewer executable and assets directory exist, then creates
    (or overwrites) a .lnk shortcut. By default the shortcut is placed on the
    current user's Desktop. Use -OutputPath to test in a temporary directory.

    If the output path contains spaces, the .lnk path should be quoted when
    passing it from cmd.exe, e.g.:
        powershell -File create_desktop_shortcut.ps1 -OutputPath "C:\Temp\My Shortcut.lnk"

.PARAMETER BaseDir
    Directory containing pulse_viewer.exe and an assets directory. Defaults to
    ..\build\release\Release relative to the script's location.

.PARAMETER OutputPath
    Full path of the .lnk file to create. Defaults to the current user's Desktop.

.PARAMETER Monitor
    Optional friendly display name to append as --monitor <name>.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [string]$BaseDir = $null,

    [Parameter()]
    [string]$OutputPath = $null,

    [Parameter()]
    [string]$Monitor = $null
)

$ErrorActionPreference = 'Stop'

function Join-PS51Path {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ChildPath
    )
    return [System.IO.Path]::Combine($Path, $ChildPath)
}

if ([string]::IsNullOrEmpty($BaseDir)) {
    $repoRoot = Join-PS51Path -Path $PSScriptRoot -ChildPath '..'
    $BaseDir = Join-PS51Path -Path $repoRoot -ChildPath 'build\release\Release'
}

if (-not (Test-Path $BaseDir -PathType Container)) {
    Write-Error "Base directory not found or is not a directory: $BaseDir"
    exit 1
}

$BaseDir = (Resolve-Path $BaseDir).Path

$exe = Join-PS51Path -Path $BaseDir -ChildPath 'pulse_viewer.exe'
$assets = Join-PS51Path -Path $BaseDir -ChildPath 'assets'
$ico = Join-PS51Path -Path $assets -ChildPath 'icon.ico'

if (-not (Test-Path $exe -PathType Leaf)) {
    Write-Error "Viewer executable not found: $exe"
    exit 1
}
if (-not (Test-Path $assets -PathType Container)) {
    Write-Error "Assets directory not found: $assets"
    exit 1
}
if (-not (Test-Path $ico -PathType Leaf)) {
    Write-Warning "Shortcut icon not found, using default: $ico"
    $ico = ''
}

if ([string]::IsNullOrEmpty($OutputPath)) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $OutputPath = Join-PS51Path -Path $desktop -ChildPath 'Pulse Court.lnk'
}

$OutputDir = Split-Path $OutputPath -Parent
if ($OutputDir -and (-not (Test-Path $OutputDir -PathType Container))) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$shell = New-Object -ComObject WScript.Shell
$arguments = '--setup'
if ($Monitor) {
    $arguments = "--setup --monitor `"$Monitor`""
}

$shortcut = $shell.CreateShortcut($OutputPath)
$shortcut.TargetPath = $exe
$shortcut.Arguments = $arguments
$shortcut.WorkingDirectory = $BaseDir
$shortcut.IconLocation = if ($ico) { "$ico,0" } else { '' }
$shortcut.Description = 'Pulse Court 1v1 viewer'
$shortcut.Save()

Write-Host "Shortcut created: $OutputPath" -ForegroundColor Green
Write-Host "  Target: $exe $arguments" -ForegroundColor Gray
Write-Host "  StartIn: $BaseDir" -ForegroundColor Gray
