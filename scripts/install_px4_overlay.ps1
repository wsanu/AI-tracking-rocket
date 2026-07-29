param(
    [Parameter(Mandatory = $true)]
    [string]$Px4Root
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

Copy-Item -Recurse -Force $sourceModule $targetModule
Copy-Item -Force $sourceMsg $targetMsg

Write-Host "Installed uart_tracker module to $targetModule"
Write-Host "Installed tracker_target uORB message to $targetMsg"
Write-Host ""
Write-Host "Manual PX4 edits still required:"
Write-Host "1. Add add_subdirectory(uart_tracker) to src/modules/CMakeLists.txt"
Write-Host "2. Add CONFIG_MODULES_UART_TRACKER=y to the target board default.px4board"
