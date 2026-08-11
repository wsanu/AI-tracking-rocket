$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
. (Join-Path $repoRoot "scripts\px4_overlay_common.ps1")

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)] [string]$Name,
        [AllowEmptyString()] [string]$Actual,
        [AllowEmptyString()] [string]$Expected
    )

    if ($Actual -cne $Expected) {
        throw "$Name failed.`nExpected:`n$Expected`nActual:`n$Actual"
    }
}

$cases = @(
    @{
        Name = "empty"
        Input = ""
        Expected = "uart_tracker start`ntrack_control start`n"
    },
    @{
        Name = "track-only"
        Input = "track_control start`n"
        Expected = "uart_tracker start`ntrack_control start`n"
    },
    @{
        Name = "already-correct"
        Input = "uart_tracker start`ntrack_control start`n"
        Expected = "uart_tracker start`ntrack_control start`n"
    },
    @{
        Name = "reversed"
        Input = "track_control start`nuart_tracker start`n"
        Expected = "uart_tracker start`ntrack_control start`n"
    },
    @{
        Name = "missing-both"
        Input = "#!/bin/sh`nother_module start`n"
        Expected = "#!/bin/sh`nother_module start`n`nuart_tracker start`ntrack_control start`n"
    },
    @{
        Name = "duplicates-with-unrelated-content"
        Input = "header`ntrack_control start`nkeep_me start`nuart_tracker start`ntrack_control start`nfooter`n"
        Expected = "header`nuart_tracker start`ntrack_control start`nkeep_me start`nfooter`n"
    }
)

foreach ($case in $cases) {
    $once = ConvertTo-NormalizedBoardStartup -Content $case.Input
    Assert-Equal -Name $case.Name -Actual $once -Expected $case.Expected
    Assert-Px4BoardStartupContent -Content $once
    $twice = ConvertTo-NormalizedBoardStartup -Content $once
    Assert-Equal -Name "$($case.Name) idempotence" -Actual $twice -Expected $once
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("px4-overlay-test-" + [guid]::NewGuid().ToString("N"))
$resolvedTempRoot = [IO.Path]::GetFullPath($tempRoot)
$systemTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')

if (-not $resolvedTempRoot.StartsWith($systemTempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe test temporary path: $resolvedTempRoot"
}

try {
    $px4Root = Join-Path $resolvedTempRoot "PX4-Autopilot"
    $modulesRoot = Join-Path $px4Root "src\modules"
    $sourceModule = Join-Path $resolvedTempRoot "source\uart_tracker"
    $targetModule = Join-Path $modulesRoot "uart_tracker"
    New-Item -ItemType Directory -Force -Path $sourceModule, $targetModule | Out-Null
    [IO.File]::WriteAllText((Join-Path $sourceModule "current.cpp"), "current`n", $script:Utf8NoBom)
    [IO.File]::WriteAllText((Join-Path $targetModule "stale.c"), "stale`n", $script:Utf8NoBom)

    Install-CleanOverlayModule -Px4RootPath $px4Root -SourcePath $sourceModule -ModuleName "uart_tracker" | Out-Null
    Assert-OverlayTreeMatch -SourcePath $sourceModule -TargetPath $targetModule

    if (Test-Path -LiteralPath (Join-Path $targetModule "stale.c")) {
        throw "Clean overlay install retained stale.c"
    }

    $refused = $false
    try {
        Get-SafeOverlayModuleTarget -Px4RootPath $px4Root -ModuleName "../outside" | Out-Null
    } catch {
        $refused = $true
    }
    if (-not $refused) {
        throw "Unexpected overlay module name was not rejected"
    }
} finally {
    if (Test-Path -LiteralPath $resolvedTempRoot) {
        Remove-Item -Recurse -Force -LiteralPath $resolvedTempRoot
    }
}

Write-Host "PX4 overlay PowerShell tests OK"
