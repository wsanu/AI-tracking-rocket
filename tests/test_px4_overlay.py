import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "reproduce_px4", ROOT / "scripts" / "reproduce_px4.py"
)
REPRODUCE_PX4 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPRODUCE_PX4)


class BoardStartupNormalizationTest(unittest.TestCase):
    CASES = (
        (
            "empty",
            "",
            "uart_tracker start\ntrack_control start\n",
        ),
        (
            "track-only",
            "track_control start\n",
            "uart_tracker start\ntrack_control start\n",
        ),
        (
            "already-correct",
            "uart_tracker start\ntrack_control start\n",
            "uart_tracker start\ntrack_control start\n",
        ),
        (
            "reversed",
            "track_control start\nuart_tracker start\n",
            "uart_tracker start\ntrack_control start\n",
        ),
        (
            "missing-both",
            "#!/bin/sh\nother_module start\n",
            "#!/bin/sh\nother_module start\n\nuart_tracker start\ntrack_control start\n",
        ),
        (
            "duplicates-with-unrelated-content",
            "header\ntrack_control start\nkeep_me start\nuart_tracker start\ntrack_control start\nfooter\n",
            "header\nuart_tracker start\ntrack_control start\nkeep_me start\nfooter\n",
        ),
    )

    def test_normalization_cases(self):
        for name, content, expected in self.CASES:
            with self.subTest(name=name):
                actual = REPRODUCE_PX4.normalize_board_startup(content)
                self.assertEqual(actual, expected)
                REPRODUCE_PX4.verify_board_startup_content(actual)

    def test_normalization_is_idempotent(self):
        for name, content, _ in self.CASES:
            with self.subTest(name=name):
                once = REPRODUCE_PX4.normalize_board_startup(content)
                twice = REPRODUCE_PX4.normalize_board_startup(once)
                self.assertEqual(twice, once)

    def test_verification_rejects_duplicates_and_wrong_order(self):
        invalid = (
            "uart_tracker start\nuart_tracker start\ntrack_control start\n",
            "track_control start\nuart_tracker start\n",
            "uart_tracker start && track_control start\n",
        )
        for content in invalid:
            with self.subTest(content=content):
                with self.assertRaises(RuntimeError):
                    REPRODUCE_PX4.verify_board_startup_content(content)


class CleanOverlayInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.px4_root = self.root / "PX4-Autopilot"
        self.modules_root = self.px4_root / "src" / "modules"
        self.modules_root.mkdir(parents=True)
        self.source_modules = self.root / "source" / "modules"
        (self.source_modules / "uart_tracker").mkdir(parents=True)
        (self.source_modules / "uart_tracker" / "current.cpp").write_text(
            "current\n", encoding="utf-8"
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_clean_install_removes_stale_files(self):
        target = self.modules_root / "uart_tracker"
        target.mkdir()
        (target / "stale.c").write_text("obsolete\n", encoding="utf-8")

        REPRODUCE_PX4.install_clean_module(
            self.px4_root, self.source_modules, "uart_tracker"
        )

        self.assertEqual(
            sorted(path.name for path in target.iterdir()), ["current.cpp"]
        )
        REPRODUCE_PX4.verify_tree_match(
            self.source_modules / "uart_tracker", target
        )

    def test_refuses_unexpected_module_name(self):
        with self.assertRaises(RuntimeError):
            REPRODUCE_PX4.install_clean_module(
                self.px4_root, self.source_modules, "../outside"
            )

    def test_refuses_non_directory_target(self):
        target = self.modules_root / "uart_tracker"
        target.write_text("not a directory\n", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            REPRODUCE_PX4.install_clean_module(
                self.px4_root, self.source_modules, "uart_tracker"
            )

    def test_tree_verification_rejects_extra_and_changed_files(self):
        target = self.modules_root / "uart_tracker"
        REPRODUCE_PX4.install_clean_module(
            self.px4_root, self.source_modules, "uart_tracker"
        )
        (target / "extra.c").write_text("stale\n", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            REPRODUCE_PX4.verify_tree_match(
                self.source_modules / "uart_tracker", target
            )

        (target / "extra.c").unlink()
        (target / "current.cpp").write_text("changed\n", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            REPRODUCE_PX4.verify_tree_match(
                self.source_modules / "uart_tracker", target
            )


if __name__ == "__main__":
    unittest.main()
