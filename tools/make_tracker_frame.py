#!/usr/bin/env python3
import argparse
import struct


FEEDBACK_HEAD = bytes([0x78, 0x07])
FEEDBACK_END = 0x79
CMD_PERIODIC = 0x00
CMD_MISS_DISTANCE = 0x81


def checksum8(data: bytes) -> int:
    return sum(data) & 0xFF


def make_feedback_frame(cmd0: int, cmd1: int, payload: bytes) -> bytes:
    if len(payload) > 255:
        raise ValueError("payload too long")

    body = bytes([cmd0, cmd1, len(payload)]) + payload
    return FEEDBACK_HEAD + body + bytes([checksum8(body), FEEDBACK_END])


def make_miss_distance_frame(
    offset_x: float,
    offset_y: float,
    width: int,
    height: int,
    valid: bool,
    running: bool,
    angle_mode: bool,
    channel: int,
) -> bytes:
    status = 0

    if angle_mode:
        status |= 1 << 2

    if not running:
        status |= 1 << 1

    if valid:
        status |= 1 << 0

    if angle_mode:
        payload = bytes([status, channel]) + struct.pack("<ffHH", offset_x, offset_y, width, height)
    else:
        payload = bytes([status, channel]) + struct.pack("<iiHH", int(offset_x), int(offset_y), width, height)

    return make_feedback_frame(CMD_PERIODIC, CMD_MISS_DISTANCE, payload)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build one Huiyan V3.1 miss-distance feedback frame.")
    parser.add_argument("--offset-x", type=float, required=True, help="right positive, pixels by default")
    parser.add_argument("--offset-y", type=float, required=True, help="up positive, pixels by default")
    parser.add_argument("--w", type=int, required=True, help="target box width in pixels")
    parser.add_argument("--h", type=int, required=True, help="target box height in pixels")
    parser.add_argument("--valid", action="store_true")
    parser.add_argument("--stopped", action="store_true")
    parser.add_argument("--angle-mode", action="store_true", help="encode offsets as float angles in degrees")
    parser.add_argument("--channel", type=int, default=0)
    args = parser.parse_args()

    frame = make_miss_distance_frame(
        args.offset_x,
        args.offset_y,
        args.w,
        args.h,
        args.valid,
        not args.stopped,
        args.angle_mode,
        args.channel,
    )
    print(frame.hex(" "))


if __name__ == "__main__":
    main()
