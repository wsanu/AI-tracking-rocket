param(
    [Parameter(Mandatory = $true)] [string]$Px4Root,
    [string[]]$BoardConfigs = @("boards\hkust\nxt-dual\default.px4board", "boards\px4\sitl\default.px4board")
)
$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$px4RootPath = Resolve-Path $Px4Root
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
function Replace-ExactOnce {
    param([string]$RelativePath, [string]$Before, [string]$After)
    $path = Join-Path $px4RootPath $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing PX4 patch target: $RelativePath" }
    $content = [IO.File]::ReadAllText($path)
    if ($content.Contains($After)) { Write-Host "TRACK patch already present: $RelativePath"; return }
    if (-not $content.Contains($Before)) { throw "TRACK patch anchor not found in $RelativePath" }
    [IO.File]::WriteAllText($path, $content.Replace($Before, $After), $utf8NoBom)
    Write-Host "Applied TRACK patch: $RelativePath"
}
$sourceTrackModule = Join-Path $repoRoot "px4_tracker_integration\src\modules\track_control"
$targetTrackModule = Join-Path $px4RootPath "src\modules\track_control"
New-Item -ItemType Directory -Force -Path $targetTrackModule | Out-Null
Copy-Item -Recurse -Force (Join-Path $sourceTrackModule "*") $targetTrackModule
foreach ($message in @("TrackerTarget.msg", "TrackStatus.msg")) { Copy-Item -Force (Join-Path $repoRoot "px4_tracker_integration\msg\$message") (Join-Path $px4RootPath "msg\$message") }
$msgCMakePath = Join-Path $px4RootPath "msg\CMakeLists.txt"
$msgCMakeContent = [IO.File]::ReadAllText($msgCMakePath)
if ($msgCMakeContent -notmatch "(?m)^\s*TrackStatus\.msg\s*$") {
    $msgCMakeContent = $msgCMakeContent -replace "(?m)^(\s*TrackerTarget\.msg\s*)$", "`$1`r`n`tTrackStatus.msg"
    [IO.File]::WriteAllText($msgCMakePath, $msgCMakeContent, $utf8NoBom)
}
Replace-ExactOnce "msg\VehicleStatus.msg" "uint8 NAVIGATION_STATE_FREE3 = 9" "uint8 NAVIGATION_STATE_TRACK = 9                # Camera target pointing mode"
Replace-ExactOnce "src\modules\commander\module.yaml" "                13: Precision Land" "                13: Precision Land`n                16: Track"
Replace-ExactOnce "src\modules\manual_control\ManualControl.cpp" "`t`tcase 15: return vehicle_status_s::NAVIGATION_STATE_AUTO_VTOL_TAKEOFF;" "`t`tcase 15: return vehicle_status_s::NAVIGATION_STATE_AUTO_VTOL_TAKEOFF;`n`t`tcase 16: return vehicle_status_s::NAVIGATION_STATE_TRACK;"
$trackControlMode = "`tcase vehicle_status_s::NAVIGATION_STATE_TRACK:`n`t`tvehicle_control_mode.flag_control_manual_enabled = true;`n`t`tvehicle_control_mode.flag_control_attitude_enabled = true;`n`t`tvehicle_control_mode.flag_control_rates_enabled = true;`n`t`tvehicle_control_mode.flag_control_allocation_enabled = true;`n`t`tbreak;`n`n`tcase vehicle_status_s::NAVIGATION_STATE_ALTCTL:`n"
Replace-ExactOnce "src\modules\commander\ModeUtil\control_mode.cpp" "`tcase vehicle_status_s::NAVIGATION_STATE_ALTCTL:`n" $trackControlMode
$trackRequirements = "`t// NAVIGATION_STATE_TRACK`n`tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_angular_velocity);`n`tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_attitude);`n`tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_manual_control);`n`tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_prevent_arming);`n`n`t// NAVIGATION_STATE_AUTO_TAKEOFF`n"
Replace-ExactOnce "src\modules\commander\ModeUtil\mode_requirements.cpp" "`t// NAVIGATION_STATE_AUTO_TAKEOFF`n" $trackRequirements
Replace-ExactOnce "src\modules\mc_att_control\mc_att_control.hpp" "`tuint8_t _quat_reset_counter{0};" "`tuint8_t _nav_state{vehicle_status_s::NAVIGATION_STATE_MAX};`n`tuint8_t _quat_reset_counter{0};"
Replace-ExactOnce "src\modules\mc_att_control\mc_att_control_main.cpp" "`t`t`t`t_vehicle_type_rotary_wing = (vehicle_status.vehicle_type == vehicle_status_s::VEHICLE_TYPE_ROTARY_WING);" "`t`t`t`t_nav_state = vehicle_status.nav_state;`n`t`t`t`t_vehicle_type_rotary_wing = (vehicle_status.vehicle_type == vehicle_status_s::VEHICLE_TYPE_ROTARY_WING);"
Replace-ExactOnce "src\modules\mc_att_control\mc_att_control_main.cpp" "`t`t`t    !_vehicle_control_mode.flag_control_position_enabled) {" "`t`t`t    !_vehicle_control_mode.flag_control_position_enabled &&`n`t`t`t    _nav_state != vehicle_status_s::NAVIGATION_STATE_TRACK) {"
Replace-ExactOnce "src\lib\modes\ui.hpp" "`t       (1u << vehicle_status_s::NAVIGATION_STATE_POSITION_SLOW) |" "`t       (1u << vehicle_status_s::NAVIGATION_STATE_POSITION_SLOW) |`n`t       (1u << vehicle_status_s::NAVIGATION_STATE_TRACK) |"
Replace-ExactOnce "src\lib\modes\ui.hpp" "`t`"9: unallocated`"," "`t`"Track`","
$boardExtras = "boards\hkust\nxt-dual\init\rc.board_extras"
$boardExtrasPath = Join-Path $px4RootPath $boardExtras
if (Test-Path -LiteralPath $boardExtrasPath) {
    $extras = [IO.File]::ReadAllText($boardExtrasPath)
    if ($extras -notmatch "(?m)^track_control start$") {
        $extras = $extras.TrimEnd() + "`r`n`r`n# TRACK must run before the RC mode can publish attitude setpoints.`r`ntrack_control start`r`n"
        [IO.File]::WriteAllText($boardExtrasPath, $extras, $utf8NoBom)
    }
}
foreach ($boardConfig in $BoardConfigs) {
    $path = Join-Path $px4RootPath $boardConfig
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $content = [IO.File]::ReadAllText($path)
    if ($content -notmatch "(?m)^CONFIG_MODULES_TRACK_CONTROL=y$") {
        if ($content -match "(?m)^CONFIG_MODULES_UART_TRACKER=y$") { $content = $content -replace "(?m)^(CONFIG_MODULES_UART_TRACKER=y)$", "`$1`r`nCONFIG_MODULES_TRACK_CONTROL=y" } else { $content = $content.TrimEnd() + "`r`nCONFIG_MODULES_TRACK_CONTROL=y`r`n" }
        [IO.File]::WriteAllText($path, $content, $utf8NoBom)
    }
}
Write-Host "Installed TRACK mode overlay to $px4RootPath"