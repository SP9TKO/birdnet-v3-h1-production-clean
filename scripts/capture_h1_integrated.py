#!/usr/bin/env python3
"""Capture one physical integrated-H1 UART run and trigger it at H1_READY."""

from __future__ import annotations

import argparse
import pathlib
import time

import serial


VISIBLE_PREFIXES = (
    b"H1_BOOT",
    b"H1_ID",
    b"H1_RUNTIME",
    b"H1_ARCH",
    b"H1_FRONTEND_IMPL",
    b"H1_MEMORY",
    b"H1_CACHE",
    b"H1_POSTPROCESS",
    b"H1_NPU",
    b"H1_READY",
    b"H1_TRIGGER",
    b"H1_FIXTURE_BEGIN",
    b"H1_COMPLETE",
    b"H1_FIXTURE_EXECUTED",
    b"H1_INTEGRATED_TEST_EXECUTED",
    b"H1_STOP",
    b"H1_FAIL",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=1800.0)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    last_progress = started
    bytes_seen = 0
    triggered = False
    scan = b""
    line_buffer = b""

    print(f"CAPTURE_OPEN port={args.port} baud={args.baud} output={args.output}", flush=True)
    with serial.Serial(args.port, args.baud, timeout=0.25) as uart, args.output.open("wb") as out:
        uart.reset_input_buffer()
        while time.monotonic() - started < args.timeout_seconds:
            chunk = uart.read(4096)
            if chunk:
                out.write(chunk)
                out.flush()
                bytes_seen += len(chunk)
                scan = (scan + chunk)[-8192:]
                line_buffer += chunk
                lines = line_buffer.split(b"\n")
                line_buffer = lines.pop()
                for raw_line in lines:
                    line = raw_line.rstrip(b"\r")
                    if line.startswith(VISIBLE_PREFIXES):
                        print(line.decode("utf-8", errors="replace"), flush=True)

                if not triggered and b"H1_READY trigger=G" in scan:
                    uart.write(b"G")
                    uart.flush()
                    triggered = True
                    print("CAPTURE_TRIGGER_SENT value=G", flush=True)

                if b"H1_STOP classification=" in scan:
                    print(
                        f"CAPTURE_COMPLETE bytes={bytes_seen} elapsed_seconds={time.monotonic() - started:.3f}",
                        flush=True,
                    )
                    return 0

            now = time.monotonic()
            if now - last_progress >= 30.0:
                print(
                    f"CAPTURE_PROGRESS bytes={bytes_seen} triggered={int(triggered)} elapsed_seconds={now - started:.1f}",
                    flush=True,
                )
                last_progress = now

    print(
        f"CAPTURE_TIMEOUT bytes={bytes_seen} triggered={int(triggered)} elapsed_seconds={time.monotonic() - started:.3f}",
        flush=True,
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
