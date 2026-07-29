#!/usr/bin/env python3
import argparse
import struct


SYNC = bytes([0xAA, 0x55])
MSG_TRACKER_TARGET = 0x01


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF

    for byte in data:
        crc ^= byte << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


def make_tracker_frame(
    image_x: int,
    image_y: int,
    box_w: int,
    box_h: int,
    confidence: int,
    valid: bool,
    source_age_ms: int,
) -> bytes:
    flags = 0x01 if valid else 0x00
    payload = struct.pack(
        "<HHHHBBI",
        image_x,
        image_y,
        box_w,
        box_h,
        confidence,
        flags,
        source_age_ms,
    )
    body = bytes([1 + len(payload), MSG_TRACKER_TARGET]) + payload
    crc = crc16_ccitt_false(body)
    return SYNC + body + struct.pack("<H", crc)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build one uart_tracker test frame.")
    parser.add_argument("--x", type=int, required=True)
    parser.add_argument("--y", type=int, required=True)
    parser.add_argument("--w", type=int, required=True)
    parser.add_argument("--h", type=int, required=True)
    parser.add_argument("--confidence", type=int, default=90)
    parser.add_argument("--valid", action="store_true")
    parser.add_argument("--source-age-ms", type=int, default=0)
    args = parser.parse_args()

    frame = make_tracker_frame(
        args.x,
        args.y,
        args.w,
        args.h,
        args.confidence,
        args.valid,
        args.source_age_ms,
    )
    print(frame.hex(" "))


if __name__ == "__main__":
    main()
