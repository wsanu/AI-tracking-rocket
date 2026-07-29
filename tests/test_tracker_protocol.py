import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from make_tracker_frame import crc16_ccitt_false, make_tracker_frame


class TrackerProtocolTest(unittest.TestCase):
    def test_crc16_known_vector(self):
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_make_tracker_frame_layout(self):
        frame = make_tracker_frame(
            image_x=640,
            image_y=360,
            box_w=120,
            box_h=80,
            confidence=90,
            valid=True,
            source_age_ms=12,
        )

        self.assertEqual(frame[0:2], bytes([0xAA, 0x55]))
        self.assertEqual(frame[2], 15)
        self.assertEqual(frame[3], 0x01)

        payload = frame[4:-2]
        decoded = struct.unpack("<HHHHBBI", payload)
        self.assertEqual(decoded, (640, 360, 120, 80, 90, 1, 12))

        received_crc = struct.unpack("<H", frame[-2:])[0]
        self.assertEqual(received_crc, crc16_ccitt_false(frame[2:-2]))


if __name__ == "__main__":
    unittest.main()
