$ErrorActionPreference = "Stop"

$required = @(
    "README.md",
    "docs\architecture.md",
    "docs\track_mode.md",
    "docs\uart_protocol.md",
    "px4_tracker_integration\msg\TrackerTarget.msg",
    "px4_tracker_integration\msg\TrackStatus.msg",
    "px4_tracker_integration\src\modules\uart_tracker\CMakeLists.txt",
    "px4_tracker_integration\src\modules\uart_tracker\Kconfig",
    "px4_tracker_integration\src\modules\uart_tracker\UartTracker.cpp",
    "px4_tracker_integration\src\modules\uart_tracker\UartTracker.hpp",
    "px4_tracker_integration\src\modules\uart_tracker\uart_tracker_params.c",
    "px4_tracker_integration\src\modules\track_control\CMakeLists.txt",
    "px4_tracker_integration\src\modules\track_control\Kconfig",
    "px4_tracker_integration\src\modules\track_control\TrackControl.cpp",
    "px4_tracker_integration\src\modules\track_control\TrackControl.hpp",
    "px4_tracker_integration\src\modules\track_control\track_control_params.c",
    "scripts\install_track_mode_overlay.ps1",
    "tools\make_tracker_frame.py",
    "tools\tracker_dashboard\index.html",
    "tools\tracker_dashboard\styles.css",
    "tools\tracker_dashboard\app.mjs",
    "tools\tracker_dashboard\protocol.mjs",
    "tests\test_tracker_protocol.py",
    "tests\test_dashboard_protocol.mjs"
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
