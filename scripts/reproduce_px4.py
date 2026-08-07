#!/usr/bin/env python3
"""Create or verify the pinned PX4 tree used by this repository.

The script is intentionally dependency-free and runs on Linux, WSL, macOS,
and Windows. It installs the same additive overlay as the PowerShell scripts,
without changing the TRACK control algorithm.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = REPO_ROOT / "reproducibility" / "px4-base.json"
MODULE_SELECTION_PATH = REPO_ROOT / "reproducibility" / "module-build-selection.json"


def load_lock():
    with LOCK_PATH.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def load_module_selection():
    with MODULE_SELECTION_PATH.open("r", encoding="utf-8") as stream:
        return json.load(stream)

def run(command, cwd=None):
    print("+", " ".join(str(part) for part in command))
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def git_output(px4_root, *args):
    return subprocess.check_output(
        ["git", "-C", str(px4_root), *args], text=True
    ).strip()


def write_text(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(content)


def replace_exact_once(px4_root, relative_path, before, after):
    path = px4_root / relative_path
    if not path.is_file():
        raise RuntimeError("Missing PX4 patch target: {}".format(relative_path))

    content = path.read_text(encoding="utf-8")
    if after in content:
        print("TRACK patch already present:", relative_path)
        return
    if before not in content:
        raise RuntimeError("TRACK patch anchor not found in {}".format(relative_path))

    write_text(path, content.replace(before, after, 1))
    print("Applied TRACK patch:", relative_path)


def register_message(px4_root, message, after_message):
    path = px4_root / "msg" / "CMakeLists.txt"
    content = path.read_text(encoding="utf-8")
    if any(line.strip() == message for line in content.splitlines()):
        print("uORB message already registered:", message)
        return

    anchor = after_message
    if anchor not in content:
        raise RuntimeError("Message registration anchor not found: {}".format(anchor))
    content = content.replace(anchor, anchor + "\n\t" + message, 1)
    write_text(path, content)
    print("Registered uORB message:", message)


def enable_board_module(path, module, preferred_anchors):
    if not path.is_file():
        print("Skipped missing board config:", path)
        return

    setting = "CONFIG_MODULES_{}=y".format(module)
    content = path.read_text(encoding="utf-8")
    if setting in content:
        print("Board module already enabled:", setting)
        return

    for anchor in preferred_anchors:
        if anchor in content:
            content = content.replace(anchor, anchor + "\n" + setting, 1)
            break
    else:
        content = content.rstrip() + "\n" + setting + "\n"
    write_text(path, content)
    print("Enabled board module:", setting)


def set_board_config(path, setting, enabled):
    if not path.is_file():
        raise RuntimeError("Missing module-selection board config: {}".format(path))

    enabled_line = "{}=y".format(setting)
    lines = path.read_text(encoding="utf-8").splitlines()
    lines = [line for line in lines if line.strip() != enabled_line]

    if enabled:
        lines.append(enabled_line)

    write_text(path, "\n".join(lines).rstrip() + "\n")
    print("{} board module: {}".format("Enabled" if enabled else "Disabled", setting))


def apply_module_selection(px4_root, selection):
    path = px4_root / selection["board_config"]
    for setting in selection["enabled"]:
        set_board_config(path, setting, True)
    for setting in selection["disabled"]:
        set_board_config(path, setting, False)


def verify_module_selection(px4_root, selection):
    path = px4_root / selection["board_config"]
    settings = {line.strip() for line in path.read_text(encoding="utf-8").splitlines()}

    for setting in selection["enabled"]:
        if "{}=y".format(setting) not in settings:
            raise RuntimeError("Module selection did not enable {}".format(setting))

    for setting in selection["disabled"]:
        if "{}=y".format(setting) in settings:
            raise RuntimeError("Module selection did not disable {}".format(setting))

def copy_overlay(px4_root):
    modules = REPO_ROOT / "px4_tracker_integration" / "src" / "modules"
    for module in ("uart_tracker", "track_control"):
        source = modules / module
        target = px4_root / "src" / "modules" / module
        shutil.copytree(str(source), str(target), dirs_exist_ok=True)
        print("Installed module:", target)

    for message in ("TrackerTarget.msg", "TrackStatus.msg"):
        source = REPO_ROOT / "px4_tracker_integration" / "msg" / message
        target = px4_root / "msg" / message
        shutil.copy2(str(source), str(target))
        print("Installed message:", target)

    register_message(px4_root, "TrackerTarget.msg", "FollowTargetStatus.msg")
    register_message(px4_root, "TrackStatus.msg", "TrackerTarget.msg")


def apply_track_patches(px4_root):
    replace_exact_once(
        px4_root,
        "msg/VehicleStatus.msg",
        "uint8 NAVIGATION_STATE_FREE3 = 9",
        "uint8 NAVIGATION_STATE_TRACK = 9                # Camera target pointing mode",
    )
    replace_exact_once(
        px4_root,
        "src/modules/commander/module.yaml",
        "                13: Precision Land",
        "                13: Precision Land\n                16: Track",
    )
    replace_exact_once(
        px4_root,
        "src/modules/manual_control/ManualControl.cpp",
        "\t\tcase 15: return vehicle_status_s::NAVIGATION_STATE_AUTO_VTOL_TAKEOFF;",
        "\t\tcase 15: return vehicle_status_s::NAVIGATION_STATE_AUTO_VTOL_TAKEOFF;\n"
        "\t\tcase 16: return vehicle_status_s::NAVIGATION_STATE_TRACK;",
    )
    replace_exact_once(
        px4_root,
        "src/modules/commander/ModeUtil/control_mode.cpp",
        "\tcase vehicle_status_s::NAVIGATION_STATE_ALTCTL:\n",
        "\tcase vehicle_status_s::NAVIGATION_STATE_TRACK:\n"
        "\t\tvehicle_control_mode.flag_control_manual_enabled = true;\n"
        "\t\tvehicle_control_mode.flag_control_attitude_enabled = true;\n"
        "\t\tvehicle_control_mode.flag_control_rates_enabled = true;\n"
        "\t\tvehicle_control_mode.flag_control_allocation_enabled = true;\n"
        "\t\tbreak;\n\n"
        "\tcase vehicle_status_s::NAVIGATION_STATE_ALTCTL:\n",
    )
    replace_exact_once(
        px4_root,
        "src/modules/commander/ModeUtil/mode_requirements.cpp",
        "\t// NAVIGATION_STATE_AUTO_TAKEOFF\n",
        "\t// NAVIGATION_STATE_TRACK\n"
        "\tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_angular_velocity);\n"
        "\tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_attitude);\n"
        "\tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_manual_control);\n"
        "\tsetRequirement(vehicle_status_s::NAVIGATION_STATE_TRACK, flags.mode_req_prevent_arming);\n\n"
        "\t// NAVIGATION_STATE_AUTO_TAKEOFF\n",
    )
    replace_exact_once(
        px4_root,
        "src/modules/mc_att_control/mc_att_control.hpp",
        "\tuint8_t _quat_reset_counter{0};",
        "\tuint8_t _nav_state{vehicle_status_s::NAVIGATION_STATE_MAX};\n"
        "\tuint8_t _quat_reset_counter{0};",
    )
    replace_exact_once(
        px4_root,
        "src/modules/mc_att_control/mc_att_control_main.cpp",
        "\t\t\t\t_vehicle_type_rotary_wing = (vehicle_status.vehicle_type == vehicle_status_s::VEHICLE_TYPE_ROTARY_WING);",
        "\t\t\t\t_nav_state = vehicle_status.nav_state;\n"
        "\t\t\t\t_vehicle_type_rotary_wing = (vehicle_status.vehicle_type == vehicle_status_s::VEHICLE_TYPE_ROTARY_WING);",
    )
    replace_exact_once(
        px4_root,
        "src/modules/mc_att_control/mc_att_control_main.cpp",
        "\t\t\t    !_vehicle_control_mode.flag_control_position_enabled) {",
        "\t\t\t    !_vehicle_control_mode.flag_control_position_enabled &&\n"
        "\t\t\t    _nav_state != vehicle_status_s::NAVIGATION_STATE_TRACK) {",
    )
    replace_exact_once(
        px4_root,
        "src/lib/modes/ui.hpp",
        "\t       (1u << vehicle_status_s::NAVIGATION_STATE_POSITION_SLOW) |",
        "\t       (1u << vehicle_status_s::NAVIGATION_STATE_POSITION_SLOW) |\n"
        "\t       (1u << vehicle_status_s::NAVIGATION_STATE_TRACK) |",
    )
    replace_exact_once(
        px4_root,
        "src/lib/modes/ui.hpp",
        '\t"9: unallocated",',
        '\t"Track",',
    )


def configure_boards(px4_root, board_configs):
    for relative in board_configs:
        path = px4_root / relative
        enable_board_module(
            path,
            "UART_TRACKER",
            ("CONFIG_MODULES_UXRCE_DDS_CLIENT=y", "CONFIG_MODULES_SENSORS=y"),
        )
        enable_board_module(
            path,
            "TRACK_CONTROL",
            ("CONFIG_MODULES_UART_TRACKER=y",),
        )

    extras = px4_root / "boards" / "hkust" / "nxt-dual" / "init" / "rc.board_extras"
    if extras.is_file():
        content = extras.read_text(encoding="utf-8")
        if "track_control start" not in content:
            content = content.rstrip() + (
                "\n\n# TRACK must run before the RC mode can publish attitude setpoints.\n"
                "track_control start\n"
            )
            write_text(extras, content)
            print("Enabled track_control startup:", extras)


def verify_overlay(px4_root, lock, board_configs, module_selection):
    expected_files = (
        "msg/TrackerTarget.msg",
        "msg/TrackStatus.msg",
        "src/modules/uart_tracker/UartTracker.cpp",
        "src/modules/uart_tracker/uart_tracker_params.c",
        "src/modules/track_control/TrackControl.cpp",
    )
    for relative in expected_files:
        if not (px4_root / relative).is_file():
            raise RuntimeError("Overlay verification failed, missing {}".format(relative))

    checks = {
        "msg/VehicleStatus.msg": "NAVIGATION_STATE_TRACK = 9",
        "src/modules/commander/module.yaml": "16: Track",
        "src/modules/manual_control/ManualControl.cpp": "case 16: return vehicle_status_s::NAVIGATION_STATE_TRACK;",
        "src/lib/modes/ui.hpp": '"Track",',
    }
    for relative, marker in checks.items():
        if marker not in (px4_root / relative).read_text(encoding="utf-8"):
            raise RuntimeError("Overlay verification failed in {}".format(relative))

    for relative in board_configs:
        path = px4_root / relative
        if path.is_file():
            content = path.read_text(encoding="utf-8")
            for setting in ("CONFIG_MODULES_UART_TRACKER=y", "CONFIG_MODULES_TRACK_CONTROL=y"):
                if setting not in content:
                    raise RuntimeError("Missing {} in {}".format(setting, relative))

    verify_module_selection(px4_root, module_selection)

    head = git_output(px4_root, "rev-parse", "HEAD")
    if head != lock["commit"]:
        raise RuntimeError("PX4 HEAD is {}, expected {}".format(head, lock["commit"]))
    print("Overlay verification OK")
    print("PX4 commit:", head)
    print("Build target:", lock["board_target"])


def prepare_checkout(px4_root, lock, clone, update_submodules):
    if not px4_root.exists():
        if not clone:
            raise RuntimeError("PX4 root does not exist; pass --clone to create it")
        px4_root.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", lock["repository"], px4_root])

    if not (px4_root / ".git").exists():
        raise RuntimeError("PX4 root is not a Git checkout: {}".format(px4_root))

    head = git_output(px4_root, "rev-parse", "HEAD")
    if head != lock["commit"]:
        tracked_changes = git_output(px4_root, "status", "--porcelain", "--untracked-files=no")
        if tracked_changes:
            raise RuntimeError("Refusing to change PX4 revision because tracked files are modified")
        run(["git", "checkout", "--detach", lock["commit"]], cwd=px4_root)

    if update_submodules:
        run(["git", "submodule", "sync", "--recursive"], cwd=px4_root)
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=px4_root)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--px4-root", required=True, type=Path, help="PX4-Autopilot directory")
    parser.add_argument("--clone", action="store_true", help="clone PX4 if the directory is absent")
    parser.add_argument("--verify-only", action="store_true", help="verify without modifying files")
    parser.add_argument(
        "--skip-submodules",
        action="store_true",
        help="do not initialize/update PX4 submodules",
    )
    parser.add_argument(
        "--board-config",
        action="append",
        dest="board_configs",
        help="relative .px4board path; may be repeated",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    lock = load_lock()
    module_selection = load_module_selection()
    px4_root = args.px4_root.expanduser().resolve()
    board_configs = args.board_configs or [
        "boards/hkust/nxt-dual/default.px4board",
        "boards/px4/sitl/default.px4board",
    ]

    if args.verify_only:
        if not px4_root.exists():
            raise RuntimeError("PX4 root does not exist: {}".format(px4_root))
        verify_overlay(px4_root, lock, board_configs, module_selection)
        return

    prepare_checkout(px4_root, lock, args.clone, not args.skip_submodules)
    copy_overlay(px4_root)
    apply_track_patches(px4_root)
    configure_boards(px4_root, board_configs)
    apply_module_selection(px4_root, module_selection)
    verify_overlay(px4_root, lock, board_configs, module_selection)
    print("Next: cd {} && make {} -j4".format(px4_root, lock["board_target"]))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print("ERROR:", error, file=sys.stderr)
        sys.exit(1)
