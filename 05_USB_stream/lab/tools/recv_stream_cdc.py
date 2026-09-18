#!/usr/bin/env python3
"""
Lab C5 - USB CDC real-time viewer.

Adapted from the SDK's mipi_capture_to_enc sample tool
(tools/recv_quadrants_cdc.py). The wire format (16-byte "QBUS" header +
JPEG payload, CRC32-checked) is identical; the difference is that this
lab streams a single continuous 480x270 frame instead of four fixed
960x540 quadrants, so this script loops forever showing each frame in a
live window instead of stepping through 4 frames and exiting.

Usage:
    python recv_stream_cdc.py --port COM5           (Windows)
    python recv_stream_cdc.py --port /dev/ttyACM0   (Linux/WSL)

Requires: pyserial, opencv-python, numpy
    pip install pyserial opencv-python numpy
"""
import argparse
import struct
import time
import zlib
from pathlib import Path

import cv2
import numpy as np
import serial

MAGIC = 0x51425553  # "QBUS"
HDR_FMT = "<I H B B I I"
HDR_SZ = struct.calcsize(HDR_FMT)

WIN_NAME = "SR110 Lab C5 - Live Stream"


def read_exact(ser, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError("serial read timeout")
        buf += chunk
    return bytes(buf)


def read_hdr_resync(ser):
    """Read a header and resync if the stream is misaligned."""
    buf = bytearray(read_exact(ser, HDR_SZ))
    while True:
        magic, ver, frame_id, _flags, length, crc = struct.unpack(HDR_FMT, buf)
        if magic == MAGIC and ver == 1:
            return magic, ver, frame_id, _flags, length, crc
        buf = buf[1:] + read_exact(ser, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0, help="Serial read timeout (seconds)")
    ap.add_argument("--save-dir", default=None,
                    help="If set, also save each received frame as <save-dir>/frame_<id>.jpg")
    ap.add_argument("--verify-crc", action="store_true", help="Verify CRC32 of each frame")
    args = ap.parse_args()

    save_dir = Path(args.save_dir) if args.save_dir else None
    if save_dir is not None:
        save_dir.mkdir(parents=True, exist_ok=True)

    cv2.namedWindow(WIN_NAME, cv2.WINDOW_AUTOSIZE)

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
        # Toggling DTR low->high makes the device notice the host connected
        # even on host stacks that don't emit a control-line-state change
        # unless DTR actually changes value.
        ser.dtr = False
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        time.sleep(0.1)
        ser.dtr = True

        print("Waiting for frames... (press 'q' in the video window to quit)")

        frame_count = 0
        t_start = time.time()

        while True:
            try:
                magic, ver, frame_id, _flags, length, crc = read_hdr_resync(ser)
                jpeg = read_exact(ser, length)
            except TimeoutError:
                print("Serial read timeout - device stopped sending?")
                break

            if args.verify_crc:
                actual_crc = zlib.crc32(jpeg) & 0xFFFFFFFF
                if actual_crc != crc:
                    print(f"Warning: CRC mismatch on frame {frame_id} "
                          f"(expected {crc:#x}, got {actual_crc:#x}), skipping")
                    continue

            img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
            if img is None:
                print(f"Warning: failed to decode frame {frame_id}, skipping")
                continue

            frame_count += 1
            elapsed = time.time() - t_start
            fps = frame_count / elapsed if elapsed > 0 else 0.0

            cv2.putText(img, f"frame {frame_id}  {fps:.1f} fps", (10, 24),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow(WIN_NAME, img)

            if save_dir is not None:
                out = save_dir / f"frame_{frame_id:05d}.jpg"
                out.write_bytes(jpeg)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                print("Quit requested by user")
                break

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
