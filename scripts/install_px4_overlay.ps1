param(
    [Parameter(Mandatory = $true)]
    [string]$Px4Root,

    [string[]]$BoardConfigs = @(
        "boards\px4\fmu-v6x\default.px4board",
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
$sourceMsg = Join-Path $repoRoot "px4_tracker_integration\msg\tracker_target.msg"
$targetModule = Join-Path $modulesDir "uart_tracker"
$targetMsg = Join-Path $msgDir "tracker_target.msg"

New-Item -ItemType Directory -Force -Path $targetModule | Out-Null
Copy-Item -Recurse -Force (Join-Path $sourceModule "*") $targetModule
Copy-Item -Force $sourceMsg $targetMsg

Write-Host "Installed uart_tracker module to $targetModule"
Write-Host "Installed tracker_target uORB message to $targetMsg"

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

    Set-Content -LiteralPath $path -Value $content -NoNewline -Encoding UTF8
    Write-Host "Enabled CONFIG_MODULES_UART_TRACKER in $boardConfig"
}
