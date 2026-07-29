$ErrorActionPreference = "Stop"

$required = @(
    "README.md",
    "docs\architecture.md",
    "docs\uart_protocol.md",
    "px4_tracker_integration\msg\tracker_target.msg",
    "px4_tracker_integration\src\modules\uart_tracker\CMakeLists.txt",
    "px4_tracker_integration\src\modules\uart_tracker\Kconfig",
    "px4_tracker_integration\src\modules\uart_tracker\UartTracker.cpp",
    "px4_tracker_integration\src\modules\uart_tracker\UartTracker.hpp",
    "tools\make_tracker_frame.py",
    "tests\test_tracker_protocol.py"
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$missing = @()

foreach ($file in $required) {
    $path = Join-Path $repoRoot $file

    if (-not (Test-Path $path)) {
        $missing += $file
    }
}

if ($missing.Count -gt 0) {
    Write-Error ("Missing required files: " + ($missing -join ", "))
}

Write-Host "Repository layout OK"
