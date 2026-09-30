#!/usr/bin/env python3
"""Run one isolated M55 spectral stage and retain compact board evidence."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import struct
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from h1_usb_client import H1Client, PING  # noqa: E402

RUN_M55_SPECTRAL_FRAME = 18
GET_M55_SPECTRAL_DATA = 19
GET_M55_SPECTRAL_DIAGNOSTIC = 20
GET_M55_SPECTRAL_TIMING = 21
NAMES = {
    0: "reflected_frame",
    1: "windowed_frame",
    2: "real",
    3: "imag",
    4: "power_hypot_squared",
    5: "power_real_squared_plus_imag",
}
LENGTHS = {0: 2048, 1: 2048, 2: 1025, 3: 1025, 4: 1025, 5: 1025}
STAGES = {
    "init": 1,
    "frame": 2,
    "hann": 3,
    "cfft": 4,
    "split": 5,
    "power-hypot": 6,
    "power-squares": 7,
    "power-cmsis": 8,
}
TIMING_FIELDS = {
    "frame_preparation": "frame",
    "hann": "hann",
    "cfft": "cfft",
    "real_split": "split",
    "cfft_plus_split": "cfft_split",
    "power_hypot": "hypot",
    "power_squares": "squares",
    "power_cmsis_mag_squared": "cmsis",
}


def timestamp() -> str:
    return datetime.now(timezone.utc).isoformat()


def save_record(output: Path, record: dict[str, Any]) -> None:
    (output / "run.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


def prepare_output(path: Path) -> None:
    if path.exists():
        raise FileExistsError(f"refusing to overwrite existing evidence directory: {path}")
    path.mkdir(parents=True)


def collect_timing(client: H1Client, record: dict[str, Any], timeout: float) -> bool:
    try:
        raw = client.command(GET_M55_SPECTRAL_TIMING, timeout=min(timeout, 5.0)).value
    except Exception as exc:
        record["timing_error"] = f"{type(exc).__name__}: {exc}"
        return False
    record["timing_raw"] = raw
    clock_hz = int(raw.get("clock_hz", raw.get("h", 0)))
    if "e" in raw or clock_hz <= 0:
        record["timing_error"] = "timing query returned an invalid or zero-frequency counter record"
        return False
    aliases = {
        "frame_preparation": ("frame",),
        "hann": ("hann",),
        "cfft": ("cfft",),
        "real_split": ("split",),
        "cfft_plus_split": ("cfft", "split"),
        "power_squares": ("scalar",),
        "power_cmsis_mag_squared": ("cmsis",),
    }
    per_stage: dict[str, dict[str, float | int]] = {}
    if isinstance(raw.get("c"), list) and len(raw["c"]) == 8:
        values = dict(zip(TIMING_FIELDS, raw["c"], strict=True))
        for name, oldkey in TIMING_FIELDS.items():
            cycles = int(values[name])
            per_stage[name] = {
                "cycles": cycles,
                "microseconds": round(cycles * 1_000_000 / clock_hz, 3),
                "projected_188_microseconds": round(cycles * 188 * 1_000_000 / clock_hz, 3),
            }
    else:
        for name, keys in aliases.items():
            if all(key in raw and isinstance(raw[key], int) for key in keys):
                cycles = sum(int(raw[key]) for key in keys)
                per_stage[name] = {
                    "cycles": cycles,
                    "microseconds": round(cycles * 1_000_000 / clock_hz, 3),
                    "projected_188_microseconds": round(cycles * 188 * 1_000_000 / clock_hz, 3),
                }
    if not per_stage:
        record["timing_error"] = "timing response contained no recognized stage counters"
        return False
    record["timing"] = {"clock_hz": clock_hz, "stages": per_stage}
    return True


def capture_spectral_data(client: H1Client, output: Path, cmsis: bool) -> None:
    names = dict(NAMES)
    if cmsis:
        names[5] = "power_cmsis_mag_squared"
    for kind, name in names.items():
        values: list[int] = []
        for offset in range(0, LENGTHS[kind], 64):
            count = min(64, LENGTHS[kind] - offset)
            data = client.command(
                GET_M55_SPECTRAL_DATA,
                struct.pack("<III", kind, offset, count),
            ).value
            values.extend(int(item, 16) for item in data["values"])
        if len(values) != LENGTHS[kind]:
            raise RuntimeError(f"{name}: retrieved {len(values)} values")
        (output / f"{name}.f32le").write_bytes(struct.pack(f"<{len(values)}I", *values))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--stop-after", choices=STAGES, default="init")
    query_mode = parser.add_mutually_exclusive_group()
    query_mode.add_argument(
        "--diagnostic-only",
        action="store_true",
        help="send exactly PING, type 20, PING without running a spectral stage",
    )
    query_mode.add_argument(
        "--timing-only",
        action="store_true",
        help="send exactly PING, type 21, PING without running a spectral stage",
    )
    parser.add_argument(
        "--capture-data",
        action="store_true",
        help="retrieve spectral arrays after timing capture; adds CDC traffic",
    )
    query_mode.add_argument(
        "--frame-ack-return-only",
        action="store_true",
        help="send exactly PING, type 18 frame 0/stage init, PING and stop",
    )
    parser.add_argument(
        "--expect-frame-ack-sram-marker",
        action="store_true",
        help="require the follow-up PING to confirm the internal SRAM marker",
    )
    args = parser.parse_args()
    if args.expect_frame_ack_sram_marker and not args.frame_ack_return_only:
        parser.error("--expect-frame-ack-sram-marker requires --frame-ack-return-only")
    output = args.output or Path(".development-work") / (
        "m55-spectral-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    )
    prepare_output(output)
    stop_after = STAGES[args.stop_after]
    record: dict[str, Any] = {
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "mode": "FRAME_ACK_RETURN_ONLY" if args.frame_ack_return_only else (
            "DIAGNOSTIC_ONLY" if args.diagnostic_only else (
                "TIMING_ONLY" if args.timing_only else "SINGLE_STAGE"
            )
        ),
        "frame_index": 0,
        "stop_after": args.stop_after,
        "timeout_seconds": args.timeout,
    }

    with H1Client(args.port, args.timeout) as client:
        record["port"] = client.port
        try:
            ping = client.command(PING, timeout=min(args.timeout, 5.0))
            record["ping_before_run"] = ping.value
        except Exception as exc:
            record["ping_before_run_error"] = f"{type(exc).__name__}: {exc}"
            if not args.frame_ack_return_only:
                record["status"] = "PING_FAILED_BEFORE_RUN"
                record["error"] = record["ping_before_run_error"]
                save_record(output, record)
                return 2

        if args.frame_ack_return_only:
            sent = timestamp()
            started = time.monotonic()
            ack_ok = False
            try:
                ack = client.command(
                    RUN_M55_SPECTRAL_FRAME,
                    struct.pack("<II", 0, STAGES["init"]),
                    timeout=args.timeout,
                )
                record["frame_ack"] = ack.value
                record["frame_ack_payload_utf8"] = ack.payload.decode("utf-8")
                record["frame_ack_payload_hex"] = ack.payload.hex()
                record["frame_ack_payload_bytes"] = len(ack.payload)
                record["frame_ack_response_type"] = ack.message_type
                ack_ok = ack.value.get("accepted") is True
            except Exception as exc:
                record["frame_ack_error"] = f"{type(exc).__name__}: {exc}"
            record["command_sent_utc"] = sent
            record["frame_latency_seconds"] = time.monotonic() - started

            ping_started = time.monotonic()
            ping_ok = False
            try:
                ping_after = client.command(PING, timeout=min(args.timeout, 5.0))
                record["ping_after_frame"] = {
                    "status": "RESPONSE",
                    "elapsed_seconds": time.monotonic() - ping_started,
                    "response": ping_after.value,
                }
                ping_ok = ping_after.value.get("pong") is True
            except Exception as exc:
                record["ping_after_frame"] = {
                    "status": "TIMEOUT_OR_ERROR",
                    "elapsed_seconds": time.monotonic() - ping_started,
                    "error": f"{type(exc).__name__}: {exc}",
                }

            marker_ok = (
                not args.expect_frame_ack_sram_marker
                or record.get("ping_after_frame", {}).get("response", {}).get(
                    "frame_ack_sram_marker"
                ) == "written"
            )
            if args.expect_frame_ack_sram_marker:
                record["expected_sram_marker"] = "written"
                record["sram_marker_confirmed"] = marker_ok
            if ack_ok and ping_ok and marker_ok and "ping_before_run_error" not in record:
                record["status"] = "FRAME_ACK_RETURN_PING_PASS"
            elif ack_ok and ping_ok and not marker_ok:
                record["status"] = "FRAME_ACK_RETURN_MARKER_MISSING"
            elif ack_ok:
                record["status"] = "FRAME_ACK_THEN_PING_FAIL"
            elif ping_ok:
                record["status"] = "FRAME_ACK_MISSING_PING_OK"
            else:
                record["status"] = "FRAME_ACK_MISSING_PING_FAIL"
            save_record(output, record)
            return 0 if record["status"] == "FRAME_ACK_RETURN_PING_PASS" else 6

        if args.diagnostic_only:
            try:
                diagnostic = client.command(
                    GET_M55_SPECTRAL_DIAGNOSTIC, timeout=min(args.timeout, 5.0)
                )
                record["diagnostic"] = diagnostic.value
            except Exception as exc:
                record["status"] = "TYPE20_QUERY_FAILED"
                record["diagnostic_error"] = f"{type(exc).__name__}: {exc}"
                save_record(output, record)
                return 3
            try:
                ping_after = client.command(PING, timeout=min(args.timeout, 5.0))
                record["ping_after_query"] = ping_after.value
            except Exception as exc:
                record["status"] = "TYPE20_RESPONSE_THEN_PING_FAILED"
                record["ping_after_error"] = f"{type(exc).__name__}: {exc}"
                save_record(output, record)
                return 4
            record["status"] = (
                "TYPE20_SMALL_PASS"
                if record["diagnostic"].get("ok") is True
                and record["ping_after_query"].get("pong") is True
                else "TYPE20_SMALL_INVALID_RESPONSE"
            )
            save_record(output, record)
            return 0 if record["status"] == "TYPE20_SMALL_PASS" else 5

        if args.timing_only:
            timing_query_ok = False
            try:
                timing_probe = client.command(
                    GET_M55_SPECTRAL_TIMING, timeout=min(args.timeout, 5.0)
                )
                record["timing_probe"] = timing_probe.value
                timing_query_ok = True
            except Exception as exc:
                record["timing_probe_error"] = f"{type(exc).__name__}: {exc}"
            try:
                ping_after = client.command(PING, timeout=min(args.timeout, 5.0))
                record["ping_after_query"] = ping_after.value
            except Exception as exc:
                record["ping_after_error"] = f"{type(exc).__name__}: {exc}"
            if not timing_query_ok:
                record["status"] = (
                    "TYPE21_QUERY_FAILED_PING_OK"
                    if record.get("ping_after_query", {}).get("pong") is True
                    else "TYPE21_QUERY_AND_FOLLOWUP_PING_FAILED"
                )
                save_record(output, record)
                return 6
            if record.get("ping_after_query", {}).get("pong") is not True:
                record["status"] = "TYPE21_RESPONSE_THEN_PING_FAILED"
                save_record(output, record)
                return 7
            record["status"] = (
                "TYPE21_TINY_PASS"
                if record["timing_probe"].get("ok") is True
                else "TYPE21_TINY_INVALID_RESPONSE"
            )
            save_record(output, record)
            return 0 if record["status"] == "TYPE21_TINY_PASS" else 8

        sent = timestamp()
        started = time.monotonic()
        try:
            ack = client.command(
                RUN_M55_SPECTRAL_FRAME,
                struct.pack("<II", 0, stop_after),
                timeout=args.timeout,
            )
        except Exception as exc:
            record["status"] = "PRE_EXECUTION_ACK_NOT_RECEIVED"
            record["command_sent_utc"] = sent
            record["ack_wait_seconds"] = time.monotonic() - started
            record["error"] = f"{type(exc).__name__}: {exc}"
            try:
                followup = client.command(PING, timeout=5.0)
                record["followup_ping"] = {"status": "RESPONSE", "response": followup.value}
            except Exception as ping_exc:
                record["followup_ping"] = {
                    "status": "TIMEOUT_OR_ERROR",
                    "error": f"{type(ping_exc).__name__}: {ping_exc}",
                }
            save_record(output, record)
            return 6

        record["command_sent_utc"] = sent
        record["ack_wait_seconds"] = time.monotonic() - started
        record["ack"] = ack.value
        if ack.value.get("accepted") is not True:
            record["status"] = "INVALID_PRE_EXECUTION_ACK"
            save_record(output, record)
            return 7

        ping_started = time.monotonic()
        try:
            ping_after = client.command(PING, timeout=5.0)
            record["ping_after_run"] = {
                "status": "RESPONSE",
                "elapsed_seconds": time.monotonic() - ping_started,
                "response": ping_after.value,
            }
        except Exception as exc:
            record["status"] = "ACK_THEN_PING_TIMEOUT"
            record["ping_after_run"] = {
                "status": "TIMEOUT_OR_ERROR",
                "elapsed_seconds": time.monotonic() - ping_started,
                "error": f"{type(exc).__name__}: {exc}",
            }
            save_record(output, record)
            return 8

        try:
            diagnostic = client.command(
                GET_M55_SPECTRAL_DIAGNOSTIC, timeout=min(args.timeout, 5.0)
            ).value
            record["diagnostic"] = diagnostic
        except Exception as exc:
            record["status"] = "PING_OK_DIAGNOSTIC_QUERY_FAILED"
            record["diagnostic_error"] = f"{type(exc).__name__}: {exc}"
            save_record(output, record)
            return 9

        completed = (
            diagnostic.get("ok") is True
            and diagnostic.get("valid") is True
            and diagnostic.get("entered") == stop_after
            and diagnostic.get("completed") == stop_after
            and diagnostic.get("exception") == 0
        )
        if not completed:
            record["status"] = "STAGE_RETURNED_INCOMPLETE"
            save_record(output, record)
            return 10

        timing_ok = collect_timing(client, record, args.timeout)
        if args.capture_data and stop_after >= STAGES["power-squares"]:
            try:
                capture_spectral_data(
                    client, output, cmsis=args.stop_after == "power-cmsis"
                )
            except Exception as exc:
                record["data_retrieval_error"] = f"{type(exc).__name__}: {exc}"
                record["status"] = "STAGE_COMPLETE_DATA_RETRIEVAL_FAILED"
                save_record(output, record)
                return 11

        record["status"] = "STAGE_COMPLETED" if timing_ok else "STAGE_COMPLETED_TIMING_UNAVAILABLE"
        save_record(output, record)
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
