import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from make_tracker_frame import checksum8, make_command_frame, make_feedback_frame, make_miss_distance_frame


class TrackerProtocolTest(unittest.TestCase):
    def test_checksum_matches_vendor_example(self):
        frame_without_checksum_end = bytes.fromhex("58 07 01 03 00 00 59")
        self.assertEqual(checksum8(frame_without_checksum_end[2:-2]), 0x04)

    def test_make_device_info_command_frame(self):
        frame = make_command_frame(0x01, 0x04)
        self.assertEqual(frame, bytes.fromhex("58 07 01 04 00 05 59"))

    def test_make_track_command_frame(self):
        payload = struct.pack("<BBHHHH", 1, 0, 960, 540, 100, 100)
        frame = make_command_frame(0x03, 0x11, payload)
        self.assertEqual(frame[:5], bytes([0x58, 0x07, 0x03, 0x11, 10]))
        self.assertEqual(frame[5:-2], payload)
        self.assertEqual(frame[-2], checksum8(frame[2:-2]))
        self.assertEqual(frame[-1], 0x59)

    def test_uart_write_runs_in_module_task(self):
        source_path = (
            Path(__file__).resolve().parents[1]
            / "px4_tracker_integration"
            / "src"
            / "modules"
            / "uart_tracker"
            / "UartTracker.cpp"
        )
        source = source_path.read_text(encoding="utf-8")
        queue_start = source.index("int UartTracker::send_frame")
        worker_start = source.index("void UartTracker::process_pending_command")
        timeout_start = source.index("void UartTracker::check_command_response_timeout")
        run_start = source.index("void UartTracker::run()")
        open_start = source.index("bool UartTracker::open_uart()")

        self.assertNotIn("::write(_fd", source[queue_start:worker_start])
        self.assertIn("_command_queued = true", source[queue_start:worker_start])
        self.assertIn("::write(_fd", source[worker_start:timeout_start])
        self.assertIn("process_pending_command();", source[run_start:open_start])

    def test_make_miss_distance_frame_layout(self):
        frame = make_miss_distance_frame(
            offset_x=25,
            offset_y=-12,
            width=120,
            height=80,
            valid=True,
            running=True,
            angle_mode=False,
            channel=2,
        )

        self.assertEqual(frame[0:2], bytes([0x78, 0x07]))
        self.assertEqual(frame[2], 0x00)
        self.assertEqual(frame[3], 0x81)
        self.assertEqual(frame[4], 14)
        self.assertEqual(frame[-1], 0x79)

        payload = frame[5:-2]
        status, channel, offset_x, offset_y, width, height = struct.unpack("<BBiiHH", payload)
        self.assertEqual(status & 0x01, 0x01)
        self.assertEqual(status & 0x02, 0x00)
        self.assertEqual(status & 0x04, 0x00)
        self.assertEqual(channel, 2)
        self.assertEqual((offset_x, offset_y, width, height), (25, -12, 120, 80))
        self.assertEqual(frame[-2], checksum8(frame[2:-2]))

    def test_make_angle_mode_frame_layout(self):
        frame = make_miss_distance_frame(
            offset_x=1.5,
            offset_y=-0.5,
            width=10,
            height=8,
            valid=True,
            running=True,
            angle_mode=True,
            channel=1,
        )

        payload = frame[5:-2]
        status, channel, offset_x, offset_y, width, height = struct.unpack("<BBffHH", payload)
        self.assertEqual(status & 0x04, 0x04)
        self.assertEqual(channel, 1)
        self.assertAlmostEqual(offset_x, 1.5)
        self.assertAlmostEqual(offset_y, -0.5)
        self.assertEqual((width, height), (10, 8))


if __name__ == "__main__":
    unittest.main()
