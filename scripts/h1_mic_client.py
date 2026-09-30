#!/usr/bin/env python3
"""Native-PDM diagnostics for the Pre-M5 integrated H1 USB protocol."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import time
from pathlib import Path
from typing import Any

from h1_usb_client import (
    ERROR_TYPE,
    GET_IDENTITY,
    GET_PROFILE,
    GET_RESULT_SUMMARY,
    GET_TOPK,
    HEADER,
    MAGIC,
    PROTOCOL_VERSION,
    PING,
    RESPONSE_BIT,
    H1Client,
    ProtocolError,
    crc32,
    print_json,
    request_json,
    require,
)


MIC_STATUS = 10
MIC_START = 11
MIC_STOP = 12
MIC_CAPTURE_INFO = 13
MIC_GET_WINDOW_INFO = 14
MIC_GET_WINDOW_PCM = 15
MIC_RUN_WINDOW = 16

MODE = {"capture-only": 1, "window-validate": 2, "h1-single": 3}
LATEST_WINDOW = (1 << 64) - 1
WINDOW_SAMPLES = 96_000
WINDOW_BYTES = WINDOW_SAMPLES * 2
STRIDE_SAMPLES = 32_000
RING_SAMPLES = 160_000
# Deliberately conservative for the Alif CDC poll-out implementation. The
# matching firmware ceiling prevents a large synchronous write from
# monopolizing the CDC TX path.
PCM_CHUNK_BYTES = 1_024
PCM_HEADER = struct.Struct("<4sHHQIIII")


def raw_command(
    client: H1Client,
    message_type: int,
    payload: bytes = b"",
    timeout: float | None = None,
) -> bytes:
    """Exchange one frame without assuming the successful payload is JSON."""
    client.sequence = (client.sequence + 1) & 0xFFFFFFFF
    sequence = client.sequence
    frame = HEADER.pack(
        MAGIC,
        PROTOCOL_VERSION,
        message_type,
        len(payload),
        sequence,
        crc32(payload),
    ) + payload
    client._write_all(frame)
    deadline = time.monotonic() + (client.timeout if timeout is None else timeout)
    header = client._read_header(deadline)
    magic, version, reply_type, length, reply_sequence, declared_crc = HEADER.unpack(header)
    if magic != MAGIC or version != PROTOCOL_VERSION:
        raise ProtocolError(f"Invalid reply header: magic={magic!r}, version={version}")
    if reply_sequence != sequence:
        raise ProtocolError(f"Reply sequence {reply_sequence} != request {sequence}")
    reply_payload = client._read_exact(length, deadline)
    actual_crc = crc32(reply_payload)
    if actual_crc != declared_crc:
        raise ProtocolError(
            f"Reply CRC mismatch: actual={actual_crc:08x}, declared={declared_crc:08x}"
        )
    if reply_type == ERROR_TYPE:
        try:
            error = json.loads(reply_payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            error = {"raw_payload_hex": reply_payload.hex()}
        raise ProtocolError(f"Board rejected command {message_type}: {error}")
    expected_type = message_type | RESPONSE_BIT
    if reply_type != expected_type:
        raise ProtocolError(f"Reply type {reply_type:#x} != expected {expected_type:#x}")
    return reply_payload


def mic_json(
    client: H1Client,
    command: int,
    payload: bytes = b"",
    timeout: float | None = None,
) -> dict[str, Any]:
    return client.command(command, payload, timeout=timeout).value


def window_info(client: H1Client, sequence: int) -> dict[str, Any]:
    return mic_json(client, MIC_GET_WINDOW_INFO, struct.pack("<Q", sequence))


def pcm_statistics(raw: bytes) -> dict[str, int]:
    require(len(raw) % 2 == 0, "PCM byte count is not int16-aligned")
    values = [sample[0] for sample in struct.iter_unpack("<h", raw)]
    require(values, "PCM window is empty")
    total = sum(values)
    sum_squares = sum(sample * sample for sample in values)
    maximum_jump = max(
        (abs(current - previous) for previous, current in zip(values, values[1:])),
        default=0,
    )

    def trunc_div(numerator: int, denominator: int) -> int:
        quotient = abs(numerator) // denominator
        return -quotient if numerator < 0 else quotient

    return {
        "sample_count": len(values),
        "minimum": min(values),
        "maximum": max(values),
        "sum": total,
        "sum_squares": sum_squares,
        "mean_micro_pcm": trunc_div(total * 1_000_000, len(values)),
        "rms_micro_pcm": int(math.sqrt(sum_squares / len(values)) * 1_000_000 + 0.5),
        "positive_clipping_count": values.count(32767),
        "negative_clipping_count": values.count(-32768),
        "zero_count": values.count(0),
        "maximum_adjacent_jump": maximum_jump,
    }


def validate_window_bytes(info: dict[str, Any], raw: bytes) -> dict[str, Any]:
    require(len(raw) == WINDOW_BYTES, f"Expected {WINDOW_BYTES} PCM bytes, got {len(raw)}")
    actual_crc = f"{crc32(raw):08x}"
    actual_sha = hashlib.sha256(raw).hexdigest()
    require(info.get("bytes") == WINDOW_BYTES, "Board window byte count changed")
    require(info.get("sample_count") == WINDOW_SAMPLES, "Board window sample count changed")
    require(info.get("crc32") == actual_crc, "Downloaded PCM CRC differs from board")
    require(info.get("sha256") == actual_sha, "Downloaded PCM SHA-256 differs from board")
    statistics = pcm_statistics(raw)
    for key, expected in statistics.items():
        require(info.get(key) == expected, f"Window {key} differs: board={info.get(key)}, host={expected}")
    sequence = info["sequence"]
    expected_start = sequence * STRIDE_SAMPLES
    require(info.get("start_sample") == expected_start, "Window start is not sequence*stride")
    require(info.get("end_sample") == expected_start + WINDOW_SAMPLES, "Window end is wrong")
    expected_physical = expected_start % RING_SAMPLES
    expected_wrap = expected_physical + WINDOW_SAMPLES > RING_SAMPLES
    require(info.get("physical_start_index") == expected_physical, "Physical ring start is wrong")
    require(info.get("wraps") is expected_wrap, "Ring-wrap flag is wrong")
    return {
        "sequence": sequence,
        "start_sample": expected_start,
        "end_sample": expected_start + WINDOW_SAMPLES,
        "physical_start_index": expected_physical,
        "wraps": expected_wrap,
        "bytes": len(raw),
        "crc32": actual_crc,
        "sha256": actual_sha,
        **statistics,
    }


def download_selected_window(
    client: H1Client,
    info: dict[str, Any],
    output: Path | None = None,
) -> tuple[bytes, dict[str, Any]]:
    result = bytearray()
    offset = 0
    while offset < WINDOW_BYTES:
        if offset % (16 * PCM_CHUNK_BYTES) == 0:
            print(
                f"MIC_VALIDATION step=pcm_download sequence={info['sequence']} "
                f"offset={offset}",
                file=sys.stderr,
                flush=True,
            )
        requested = min(PCM_CHUNK_BYTES, WINDOW_BYTES - offset)
        payload = raw_command(
            client,
            MIC_GET_WINDOW_PCM,
            struct.pack("<II", offset, requested),
        )
        require(len(payload) == PCM_HEADER.size + requested, "Unexpected PCM reply length")
        magic, version, sample_format, sequence, returned_offset, returned_bytes, total, declared_crc = (
            PCM_HEADER.unpack_from(payload)
        )
        require(magic == b"MPCM", "Invalid PCM payload magic")
        require(version == 1, "Unsupported PCM payload version")
        require(sample_format == 1, "Unsupported PCM payload format")
        require(sequence == info["sequence"], "PCM chunk belongs to another window")
        require(returned_offset == offset, "PCM chunk offset mismatch")
        require(returned_bytes == requested, "PCM chunk length mismatch")
        require(total == WINDOW_BYTES, "PCM total byte count mismatch")
        require(declared_crc == int(info["crc32"], 16), "PCM header CRC differs from window info")
        result.extend(payload[PCM_HEADER.size:])
        offset += requested
    print(
        f"MIC_VALIDATION step=pcm_download_complete sequence={info['sequence']} "
        f"bytes={offset}",
        file=sys.stderr,
        flush=True,
    )
    raw = bytes(result)
    validated = validate_window_bytes(info, raw)
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(raw)
        validated["scratch_path"] = str(output)
    return raw, validated


def wait_for_windows(
    client: H1Client,
    count: int,
    deadline: float,
    poll_interval: float,
) -> dict[str, Any]:
    latest: dict[str, Any] = {}
    while time.monotonic() < deadline:
        latest = mic_json(client, MIC_STATUS)
        if latest.get("state") == "MIC_ERROR":
            raise RuntimeError(f"Microphone entered error state: {latest}")
        if latest.get("complete_windows", 0) >= count:
            return latest
        time.sleep(poll_interval)
    raise TimeoutError(f"Timed out waiting for {count} complete windows; last status={latest}")


FAULT_FIELDS = (
    "fifo_errors",
    "slab_misses",
    "queue_overruns",
    "sequence_faults",
    "read_timeouts",
    "invalid_block_sizes",
    "ring_frontier_faults",
    "window_geometry_faults",
)


def validate_capture_info(info: dict[str, Any], minimum_windows: int) -> None:
    require(info.get("acquisition_mechanism") == "ALIF_PDM_IRQ_K_MEM_SLAB", "Wrong driver path")
    require(info.get("dma_present") is False, "Pinned driver unexpectedly claims DMA")
    require(info.get("configured_rate_hz") == 32_000, "Configured sample rate changed")
    require(info.get("format") == "SIGNED_PCM16_LE_MONO", "PCM format changed")
    require(info.get("block_bytes") == 2_048, "Block size changed")
    require(info.get("block_count") == 8, "Block count changed")
    require(info.get("ring_samples") == RING_SAMPLES, "Ring size changed")
    require(info.get("window_samples") == WINDOW_SAMPLES, "Window size changed")
    require(info.get("stride_samples") == STRIDE_SAMPLES, "Stride changed")
    require(info.get("overlap_samples") == 64_000, "Overlap changed")
    require(info.get("complete_windows", 0) >= minimum_windows, "Too few complete windows")
    for field in FAULT_FIELDS:
        require(info.get(field) == 0, f"{field} is nonzero: {info.get(field)}")
    require(info.get("driver_published_blocks", 0) >= info.get("consumed_blocks", 0), "Counter reversal")
    require(
        info["driver_published_blocks"] - info["consumed_blocks"] <= info["block_count"],
        "Published/consumed distance exceeds block capacity",
    )
    require(info.get("queue_high_water", 0) <= info["block_count"], "Queue high-water exceeds capacity")


def select_and_download(
    client: H1Client,
    sequence: int,
    pcm_dir: Path | None,
) -> tuple[dict[str, Any], bytes]:
    info = window_info(client, sequence)
    output = None if pcm_dir is None else pcm_dir / f"window-{sequence:06d}.pcm16le"
    raw, validated = download_selected_window(client, info, output)
    return validated, raw


def validate_capture_only(
    client: H1Client,
    poll_interval: float,
) -> dict[str, Any]:
    """Prove the acquisition-only path before enabling window selection or H1."""
    print("MIC_VALIDATION step=capture_only_initial_status", file=sys.stderr, flush=True)
    initial = mic_json(client, MIC_STATUS)
    require(initial.get("running") is False, "Microphone is already running")
    print("MIC_VALIDATION step=capture_only_start", file=sys.stderr, flush=True)
    start = mic_json(client, MIC_START, struct.pack("<I", MODE["capture-only"]))
    deadline = time.monotonic() + 15.0
    running = start
    try:
        while time.monotonic() < deadline:
            running = mic_json(client, MIC_STATUS)
            if running.get("total_samples_committed", 0) >= WINDOW_SAMPLES:
                break
            time.sleep(poll_interval)
        else:
            raise TimeoutError(
                "Timed out waiting for 96,000 capture-only samples; "
                f"last status={running}"
            )
        capture = mic_json(client, MIC_CAPTURE_INFO)
    finally:
        stop = mic_json(client, MIC_STOP)
    after_stop = mic_json(client, MIC_CAPTURE_INFO)
    validate_capture_info(after_stop, 1)
    require(start.get("mode") == "MIC_CAPTURE_ONLY", "Capture-only mode was not entered")
    require(running.get("state") == "MIC_CAPTURE", "Capture-only state changed unexpectedly")
    require(running.get("inference_state") == "H1_IDLE", "Capture-only invoked H1")
    require(
        after_stop.get("sample_count") == after_stop.get("total_samples_committed"),
        "Capture-only statistics do not cover the full committed timeline",
    )
    return {
        "schema": "birdnet-v3-pre-m5-pdm-capture-only-validation",
        "version": 1,
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "start": start,
        "running_status": running,
        "capture_info_while_running": capture,
        "stop": stop,
        "capture_info_after_stop": after_stop,
        "checks": {
            "minimum_one_complete_input_window_captured": True,
            "all_fault_counters_zero": True,
            "frontend_not_run": True,
            "u85_not_run": True,
            "classifier_not_run": True,
            "postprocessing_not_run": True,
        },
    }


def validate_acquisition(
    client: H1Client,
    target_windows: int,
    poll_interval: float,
    rate_tolerance_percent: float,
    host_rate_tolerance_percent: float,
    pcm_dir: Path | None,
) -> dict[str, Any]:
    require(target_windows >= 5, "At least five windows are needed for explicit wrap coverage")
    print("MIC_VALIDATION step=initial_status", file=sys.stderr, flush=True)
    initial = mic_json(client, MIC_STATUS)
    print("MIC_VALIDATION step=start_capture", file=sys.stderr, flush=True)
    require(initial.get("running") is False, "Microphone is already running")
    started_wall = time.time()
    started_monotonic = time.monotonic()
    start = mic_json(client, MIC_START, struct.pack("<I", MODE["window-validate"]))
    print("MIC_VALIDATION step=capture_started", file=sys.stderr, flush=True)
    early_windows: list[dict[str, Any]] = []
    downloaded: dict[str, dict[str, Any]] = {}
    try:
        for sequence in range(5):
            print(
                f"MIC_VALIDATION step=wait_early_window sequence={sequence}",
                file=sys.stderr,
                flush=True,
            )
            wait_for_windows(
                client,
                sequence + 1,
                started_monotonic + 30.0,
                poll_interval,
            )
            print(
                f"MIC_VALIDATION step=window_info sequence={sequence}",
                file=sys.stderr,
                flush=True,
            )
            info = window_info(client, sequence)
            early_windows.append(info)
            if sequence in (0, 4):
                print(
                    f"MIC_VALIDATION step=window_download sequence={sequence}",
                    file=sys.stderr,
                    flush=True,
                )
                validated, _ = select_and_download(client, sequence, pcm_dir)
                downloaded[str(sequence)] = validated

        estimated_seconds = (WINDOW_SAMPLES + (target_windows - 1) * STRIDE_SAMPLES) / 32_000
        print(
            f"MIC_VALIDATION step=wait_sustained target_windows={target_windows}",
            file=sys.stderr,
            flush=True,
        )
        final_status = wait_for_windows(
            client,
            target_windows,
            started_monotonic + max(60.0, estimated_seconds * 1.15 + 15.0),
            poll_interval,
        )
        captured_monotonic = time.monotonic()
        print("MIC_VALIDATION step=capture_info", file=sys.stderr, flush=True)
        capture = mic_json(client, MIC_CAPTURE_INFO)
    finally:
        print("MIC_VALIDATION step=stop_capture", file=sys.stderr, flush=True)
        stop = mic_json(client, MIC_STOP)
    print("MIC_VALIDATION step=capture_info_after_stop", file=sys.stderr, flush=True)
    stopped_capture = mic_json(client, MIC_CAPTURE_INFO)

    validate_capture_info(stopped_capture, target_windows)
    for sequence, info in enumerate(early_windows):
        require(info["sequence"] == sequence, "Early window sequence mismatch")
        require(info["start_sample"] == sequence * STRIDE_SAMPLES, "Window stride mismatch")
        require(info["end_sample"] == sequence * STRIDE_SAMPLES + WINDOW_SAMPLES, "Window end mismatch")
        if sequence:
            require(
                info["start_sample"] - early_windows[sequence - 1]["start_sample"]
                == STRIDE_SAMPLES,
                "Adjacent window geometry mismatch",
            )
    require(downloaded["0"]["wraps"] is False, "Window 0 did not exercise non-wrap geometry")
    require(downloaded["4"]["wraps"] is True, "Window 4 did not exercise wrap geometry")
    require(downloaded["4"]["physical_start_index"] == 128_000, "Near-end wrap start changed")

    measured_rate_hz = stopped_capture["measured_rate_millihz"] / 1000.0
    configured_rate_hz = stopped_capture["configured_rate_hz"]
    device_error_percent = abs(measured_rate_hz - configured_rate_hz) * 100.0 / configured_rate_hz
    require(
        device_error_percent <= rate_tolerance_percent,
        f"Device-timed sample rate error {device_error_percent:.6f}% exceeds criterion",
    )
    host_elapsed = captured_monotonic - started_monotonic
    host_rate_hz = capture["total_samples_committed"] / host_elapsed
    host_error_percent = abs(host_rate_hz - configured_rate_hz) * 100.0 / configured_rate_hz
    require(
        host_error_percent <= host_rate_tolerance_percent,
        f"Host-timed sample rate error {host_error_percent:.6f}% exceeds criterion",
    )

    return {
        "schema": "birdnet-v3-pre-m5-pdm-acquisition-validation",
        "version": 1,
        "classification": "PDM_SLIDING_WINDOW_EXECUTED",
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "started_unix": started_wall,
        "completed_unix": time.time(),
        "prospective_criteria": {
            "target_complete_windows": target_windows,
            "device_rate_tolerance_percent": rate_tolerance_percent,
            "host_rate_tolerance_percent": host_rate_tolerance_percent,
            "required_fault_count_each": 0,
        },
        "start": start,
        "early_window_geometry": early_windows,
        "downloaded_windows": downloaded,
        "final_running_status": final_status,
        "capture_info_while_running": capture,
        "stop": stop,
        "capture_info_after_stop": stopped_capture,
        "sample_rate": {
            "configured_hz": configured_rate_hz,
            "device_irq_timed_hz": measured_rate_hz,
            "device_error_percent": device_error_percent,
            "host_elapsed_seconds": host_elapsed,
            "host_committed_samples_per_second": host_rate_hz,
            "host_error_percent": host_error_percent,
        },
        "checks": {
            "all_fault_counters_zero": True,
            "absolute_window_geometry_exact": True,
            "non_wrap_window_host_verified": True,
            "wrap_window_host_verified": True,
            "near_ring_end_window_host_verified": True,
            "pcm_crc32_sha256_statistics_match": True,
            "transport_excluded_from_acquisition_timing": True,
        },
    }


def pcm16_to_float32_identity(raw: bytes) -> dict[str, Any]:
    converted = bytearray(WINDOW_SAMPLES * 4)
    for index, (sample,) in enumerate(struct.iter_unpack("<h", raw)):
        struct.pack_into("<f", converted, index * 4, sample / 32768.0)
    data = bytes(converted)
    return {
        "bytes": len(data),
        "crc32": f"{crc32(data):08x}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "conversion": "float32(int16_sample / 32768.0)",
    }


def run_h1_single(
    client: H1Client,
    poll_interval: float,
    pcm_output: Path | None,
) -> dict[str, Any]:
    print("MIC_VALIDATION step=h1_initial_status", file=sys.stderr, flush=True)
    initial = mic_json(client, MIC_STATUS)
    require(initial.get("running") is False, "Microphone is already running")
    print("MIC_VALIDATION step=h1_start_capture", file=sys.stderr, flush=True)
    start = mic_json(client, MIC_START, struct.pack("<I", MODE["h1-single"]))
    try:
        print("MIC_VALIDATION step=h1_wait_window", file=sys.stderr, flush=True)
        ready = wait_for_windows(client, 1, time.monotonic() + 15.0, poll_interval)
        print("MIC_VALIDATION step=h1_window_info", file=sys.stderr, flush=True)
        info = window_info(client, LATEST_WINDOW)
        print("MIC_VALIDATION step=h1_download_window", file=sys.stderr, flush=True)
        raw, pcm = download_selected_window(client, info, pcm_output)
        float_identity = pcm16_to_float32_identity(raw)
        print("MIC_VALIDATION step=h1_capture_info_before_inference", file=sys.stderr, flush=True)
        capture_before_inference = mic_json(client, MIC_CAPTURE_INFO)
    finally:
        print("MIC_VALIDATION step=h1_stop_before_inference", file=sys.stderr, flush=True)
        stop = mic_json(client, MIC_STOP)
    print("MIC_VALIDATION step=h1_capture_info_after_stop", file=sys.stderr, flush=True)
    after_stop = mic_json(client, MIC_CAPTURE_INFO)
    validate_capture_info(after_stop, 1)
    print("MIC_VALIDATION step=h1_execute_frozen_window", file=sys.stderr, flush=True)
    run = mic_json(client, MIC_RUN_WINDOW, timeout=180.0)
    print("MIC_VALIDATION step=h1_topk", file=sys.stderr, flush=True)
    topk = request_json(client, GET_TOPK)
    print("MIC_VALIDATION step=h1_profile", file=sys.stderr, flush=True)
    profile = request_json(client, GET_PROFILE)
    print("MIC_VALIDATION step=h1_summary", file=sys.stderr, flush=True)
    summary = request_json(client, GET_RESULT_SUMMARY)
    print("MIC_VALIDATION step=h1_capture_info_after_result", file=sys.stderr, flush=True)
    capture_after_result = mic_json(client, MIC_CAPTURE_INFO)
    require(run.get("fixture_identity") == "microphone", "H1 did not identify microphone input")
    require(run.get("input_crc32") == float_identity["crc32"], "H1 float input CRC mismatch")
    require(summary.get("input_crc32") == float_identity["crc32"], "Summary float input CRC mismatch")
    require(summary.get("input_sha256") == float_identity["sha256"], "Summary float input SHA mismatch")
    return {
        "schema": "birdnet-v3-pre-m5-pdm-h1-single",
        "version": 1,
        "classification": "PDM_SLIDING_WINDOW_EXECUTED",
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "start": start,
        "ready": ready,
        "selected_pcm": pcm,
        "float32_input_identity": float_identity,
        "run": run,
        "topk": topk,
        "profile": profile,
        "summary": summary,
        "capture_before_inference": capture_before_inference,
        "stop_before_inference": stop,
        "capture_after_stop": after_stop,
        "capture_after_result": capture_after_result,
        "inference_execution_capture_state": "MIC_OFF_WITH_IMMUTABLE_SELECTED_WINDOW",
        "continuous_producer_architecture_preserved": True,
        "acquisition_correctness_separate_from_inference_cadence": True,
    }


def validate_hardware(
    client: H1Client,
    target_windows: int,
    poll_interval: float,
    rate_tolerance_percent: float,
    host_rate_tolerance_percent: float,
    pcm_dir: Path | None,
) -> dict[str, Any]:
    """Run the complete physical task in one stable CDC session."""
    print("MIC_VALIDATION step=ping", file=sys.stderr, flush=True)
    ping = request_json(client, PING)
    print("MIC_VALIDATION step=identity", file=sys.stderr, flush=True)
    identity = request_json(client, GET_IDENTITY)
    print("MIC_VALIDATION step=capture_only", file=sys.stderr, flush=True)
    capture_only = validate_capture_only(client, poll_interval)
    print("MIC_VALIDATION step=acquisition", file=sys.stderr, flush=True)
    acquisition = validate_acquisition(
        client,
        target_windows,
        poll_interval,
        rate_tolerance_percent,
        host_rate_tolerance_percent,
        pcm_dir,
    )
    h1_pcm = None if pcm_dir is None else pcm_dir / "h1-window-000000.pcm16le"
    h1 = run_h1_single(client, poll_interval, h1_pcm)
    return {
        "schema": "birdnet-v3-pre-m5-pdm-complete-physical-validation",
        "version": 1,
        "classification": "PDM_SLIDING_WINDOW_EXECUTED",
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "ping": ping,
        "identity": identity,
        "capture_only": capture_only,
        "acquisition": acquisition,
        "h1_single": h1,
    }


def parse_sequence(value: str) -> int:
    if value == "latest":
        return LATEST_WINDOW
    parsed = int(value, 0)
    if parsed < 0 or parsed > LATEST_WINDOW:
        raise argparse.ArgumentTypeError("window sequence is outside uint64")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CDC path; default uses fixed VID/PID/serial discovery")
    parser.add_argument("--timeout", type=float, default=10.0, help="Per-command timeout")
    parser.add_argument("--output", type=Path, help="Write JSON response/evidence")
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("status")
    actions.add_parser("capture-info")
    start = actions.add_parser("start")
    start.add_argument("mode", choices=tuple(MODE))
    actions.add_parser("stop")
    window = actions.add_parser("window-info")
    window.add_argument("sequence", type=parse_sequence, nargs="?", default=LATEST_WINDOW)
    download = actions.add_parser("download-window")
    download.add_argument("pcm_output", type=Path)
    download.add_argument("--sequence", type=parse_sequence, default=LATEST_WINDOW)
    capture_only = actions.add_parser("validate-capture-only")
    capture_only.add_argument("--poll-interval", type=float, default=0.25)

    validate = actions.add_parser("validate-acquisition")
    validate.add_argument("--windows", type=int, default=180)
    validate.add_argument("--poll-interval", type=float, default=0.25)
    validate.add_argument("--rate-tolerance-percent", type=float, default=0.5)
    validate.add_argument("--host-rate-tolerance-percent", type=float, default=1.0)
    validate.add_argument("--pcm-dir", type=Path)
    complete = actions.add_parser("validate-hardware")
    complete.add_argument("--windows", type=int, default=180)
    complete.add_argument("--poll-interval", type=float, default=0.25)
    complete.add_argument("--rate-tolerance-percent", type=float, default=0.5)
    complete.add_argument(
        "--host-rate-tolerance-percent", type=float, default=1.0
    )
    complete.add_argument("--pcm-dir", type=Path)
    run = actions.add_parser("run-h1-single")
    run.add_argument("--poll-interval", type=float, default=0.25)
    run.add_argument("--pcm-output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with H1Client(args.port, args.timeout) as client:
        if args.action == "status":
            value = mic_json(client, MIC_STATUS)
        elif args.action == "capture-info":
            value = mic_json(client, MIC_CAPTURE_INFO)
        elif args.action == "start":
            value = mic_json(client, MIC_START, struct.pack("<I", MODE[args.mode]))
        elif args.action == "stop":
            value = mic_json(client, MIC_STOP)
        elif args.action == "window-info":
            value = window_info(client, args.sequence)
        elif args.action == "download-window":
            info = window_info(client, args.sequence)
            _, value = download_selected_window(client, info, args.pcm_output)
        elif args.action == "validate-capture-only":
            value = validate_capture_only(client, args.poll_interval)
        elif args.action == "validate-acquisition":
            value = validate_acquisition(
                client,
                args.windows,
                args.poll_interval,
                args.rate_tolerance_percent,
                args.host_rate_tolerance_percent,
                args.pcm_dir,
            )
        elif args.action == "run-h1-single":
            value = run_h1_single(client, args.poll_interval, args.pcm_output)
        elif args.action == "validate-hardware":
            value = validate_hardware(
                client,
                args.windows,
                args.poll_interval,
                args.rate_tolerance_percent,
                args.host_rate_tolerance_percent,
                args.pcm_dir,
            )
        else:
            raise AssertionError(f"Unhandled action {args.action}")
    print_json(value, args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ProtocolError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
