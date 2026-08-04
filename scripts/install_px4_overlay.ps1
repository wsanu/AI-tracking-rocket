param(
    [Parameter(Mandatory = $true)]
    [string]$Px4Root,

    [string[]]$BoardConfigs = @(
        "boards\hkust\nxt-dual\default.px4board",
        "boards\px4\sitl\default.px4board"
    )
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$px4RootPath = Resolve-Path $Px4Root

$modulesDir = Join-Path $px4RootPath "src\modules"
$msgDir = Join-Path $px4RootPath "msg"

if (-not (Test-Path (Join-Path $px4RootPath "src"))) {
    throw "PX4 root does not look valid: missing src directory"
}

if (-not (Test-Path $modulesDir)) {
    throw "PX4 root does not look valid: missing src\modules directory"
}

if (-not (Test-Path $msgDir)) {
    throw "PX4 root does not look valid: missing msg directory"
}

$sourceModule = Join-Path $repoRoot "px4_tracker_integration\src\modules\uart_tracker"
$sourceMsg = Join-Path $repoRoot "px4_tracker_integration\msg\TrackerTarget.msg"
$targetModule = Join-Path $modulesDir "uart_tracker"
$targetMsg = Join-Path $msgDir "TrackerTarget.msg"
$msgCMakePath = Join-Path $msgDir "CMakeLists.txt"

New-Item -ItemType Directory -Force -Path $targetModule | Out-Null
Copy-Item -Recurse -Force (Join-Path $sourceModule "*") $targetModule
Copy-Item -Force $sourceMsg $targetMsg

Write-Host "Installed uart_tracker module to $targetModule"
Write-Host "Installed tracker_target uORB message to $targetMsg"
$msgCMakeContent = Get-Content -Raw -LiteralPath $msgCMakePath

if ($msgCMakeContent -notmatch "(?m)^\s*TrackerTarget\.msg\s*$") {
    $msgCMakeContent = $msgCMakeContent -replace "(?m)^(\s*FollowTargetStatus\.msg\s*)$", "`$1`r`n`tTrackerTarget.msg"
    [IO.File]::WriteAllText($msgCMakePath, $msgCMakeContent, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Registered TrackerTarget.msg in msg\CMakeLists.txt"
} else {
    Write-Host "TrackerTarget.msg already registered in msg\CMakeLists.txt"
}

foreach ($boardConfig in $BoardConfigs) {
    $path = Join-Path $px4RootPath $boardConfig

    if (-not (Test-Path $path)) {
        Write-Host "Skipped missing board config: $boardConfig"
        continue
    }

    $content = Get-Content -Raw -LiteralPath $path

    if ($content -match "CONFIG_MODULES_UART_TRACKER=y") {
        Write-Host "Board config already enabled: $boardConfig"
        continue
    }

    if ($content -match "CONFIG_MODULES_UXRCE_DDS_CLIENT=y") {
        $content = $content -replace "CONFIG_MODULES_UXRCE_DDS_CLIENT=y", "CONFIG_MODULES_UXRCE_DDS_CLIENT=y`r`nCONFIG_MODULES_UART_TRACKER=y"
    } elseif ($content -match "CONFIG_MODULES_SENSORS=y") {
        $content = $content -replace "CONFIG_MODULES_SENSORS=y", "CONFIG_MODULES_SENSORS=y`r`nCONFIG_MODULES_UART_TRACKER=y"
    } else {
        $content = $content.TrimEnd() + "`r`nCONFIG_MODULES_UART_TRACKER=y`r`n"
    }

    [IO.File]::WriteAllText($path, $content, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Enabled CONFIG_MODULES_UART_TRACKER in $boardConfig"
}

# Install the additive TRACK flight-mode layer after the legacy UART overlay.
& (Join-Path $PSScriptRoot "install_track_mode_overlay.ps1") -Px4Root $px4RootPath -BoardConfigs $BoardConfigs
