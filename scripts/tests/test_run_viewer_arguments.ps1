$ErrorActionPreference = 'Stop'

# This is a dependency-free regression test. The launcher is dot-sourced with
# harmless process/filesystem commands shadowed, so no viewer or build is run.
$script:lastArgumentList = @()

function Test-Path {
    param(
        [string]$Path,
        [string]$PathType
    )
    return $true
}

function Start-Process {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$WorkingDirectory,
        [switch]$PassThru,
        [switch]$NoNewWindow
    )

    $script:lastArgumentList = @($ArgumentList)
    return [pscustomobject]@{
        HasExited = $false
        ExitCode = 0
    }
}

function Start-Sleep {
    param([int]$Seconds)
}

function Assert-Equal {
    param(
        [string]$Name,
        [AllowNull()]
        [string]$Expected,
        [AllowNull()]
        [string]$Actual
    )

    if ($Expected -cne $Actual) {
        throw "$Name failed. Expected [$Expected], got [$Actual]."
    }
}

function Assert-ArgumentList {
    param(
        [string]$Name,
        [string[]]$Expected,
        [string[]]$Actual
    )

    if ($Expected.Count -ne $Actual.Count) {
        throw "$Name failed. Expected $($Expected.Count) arguments, got $($Actual.Count)."
    }
    for ($i = 0; $i -lt $Expected.Count; $i++) {
        Assert-Equal "$Name[$i]" $Expected[$i] $Actual[$i]
    }
}

$launcher = Join-Path $PSScriptRoot '..\run_viewer.ps1'
$callerRoot = (Get-Location).Path
$replayPath = 'replays\match 01.pulse'
. $launcher -Replay $replayPath
Assert-ArgumentList 'replay path' @(
    '"--replay"',
    ('"' + [System.IO.Path]::GetFullPath((Join-Path $callerRoot $replayPath)) + '"')
) $script:lastArgumentList

$previewPath = 'C:\Users\kaust\OneDrive\Documents\AI Omega Strikers\bundles\preview.pzpb'
$logPath = 'logs\session\'
. $launcher -ChallengePreviewBundle $previewPath -ChallengeLogDir $logPath
$expectedChallengeArguments = @(
    '"--challenge"',
    '"--policy-bundle-preview"',
    '"C:\Users\kaust\OneDrive\Documents\AI Omega Strikers\bundles\preview.pzpb"',
    '"--challenge-games"',
    '"20"',
    '"--challenge-log-dir"',
    ('"' + [System.IO.Path]::GetFullPath((Join-Path $callerRoot $logPath)) + '\"')
)
Assert-ArgumentList 'preview challenge paths' $expectedChallengeArguments $script:lastArgumentList

# Exercise the actual Windows command-line parser against the joined string
# that Start-Process constructs from its ArgumentList array.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class PulseCommandLineParser
{
    [DllImport("shell32.dll", SetLastError = true)]
    private static extern IntPtr CommandLineToArgvW(
        [MarshalAs(UnmanagedType.LPWStr)] string commandLine,
        out int argc);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    public static string[] Parse(string commandLine)
    {
        int argc;
        IntPtr argv = CommandLineToArgvW(commandLine, out argc);
        if (argv == IntPtr.Zero)
            throw new InvalidOperationException("CommandLineToArgvW failed.");

        try
        {
            string[] result = new string[argc];
            for (int i = 0; i < argc; i++)
            {
                IntPtr item = Marshal.ReadIntPtr(argv, i * IntPtr.Size);
                result[i] = Marshal.PtrToStringUni(item);
            }
            return result;
        }
        finally
        {
            LocalFree(argv);
        }
    }
}
'@
$parsedChallengeArguments = [PulseCommandLineParser]::Parse(
    ($script:lastArgumentList -join ' ')
)
Assert-ArgumentList 'CommandLineToArgvW challenge values' @(
    '--challenge',
    '--policy-bundle-preview',
    $previewPath,
    '--challenge-games',
    '20',
    '--challenge-log-dir',
    [System.IO.Path]::GetFullPath((Join-Path $callerRoot $logPath))
) $parsedChallengeArguments

# Windows paths cannot contain a quote, but the quoting routine must still
# preserve one if a caller supplies an arbitrary argument value.
Assert-Equal 'embedded quote' '"value\"with"' (ConvertTo-StartProcessArgument 'value"with')
Assert-Equal 'empty argument' '""' (ConvertTo-StartProcessArgument '')
Assert-ArgumentList 'CommandLineToArgvW embedded quote' @(
    'pulse_viewer.exe',
    'value"with'
) (
    [PulseCommandLineParser]::Parse(
        ('"pulse_viewer.exe" ' + (ConvertTo-StartProcessArgument 'value"with'))
    )
)

Write-Output 'run_viewer argument forwarding tests passed.'
