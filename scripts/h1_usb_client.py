#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
# SPDX-License-Identifier: Apache-2.0
"""Minimal framed USB CDC client for the Pre-M5 integrated H1 firmware."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import statistics
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import serial
from serial.tools import list_ports


MAGIC = b"H1CP"
PROTOCOL_VERSION = 1
HEADER = struct.Struct("<4sHHIII")
RESPONSE_BIT = 0x8000
ERROR_TYPE = 0xFFFF

PING = 1
STATUS = 2
GET_IDENTITY = 3
UPLOAD_WAVEFORM = 4
RUN_UPLOADED_WAVEFORM = 5
RUN_CANONICAL = 6
GET_TOPK = 7
GET_PROFILE = 8
GET_RESULT_SUMMARY = 9

USB_VID = 0x2FE3
USB_PID = 0x0001
USB_SERIAL = "H1DEV1"
CANONICAL_RAW_SHA256 = "23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8"
CANONICAL_RAW_CRC32 = 0xCE4FC4CE
EXPECTED_SCORE_CRC32 = "4d6dd49c"
EXPECTED_TOP10 = [6240, 5770, 1800, 6018, 1870, 3410, 11104, 4584, 2902, 10782]
EXPECTED_BOUNDARY_CRC32 = {
    "frontend": "3a3b5d63",
    "backbone_input": "6dfe151f",
    "shared_feature": "22f6b297",
    "embedding": "a71cbcdd",
    "classifier_input": "366ceee3",
    "logits": "9c92a00f",
    "scores": EXPECTED_SCORE_CRC32,
}
MODEL_EXPECTATIONS = {
    "backbone": {
        "source_address": "c0000000",
        "destination_address": "a0000000",
        "bytes": 20_465_840,
        "crc32": "44f2a9cb",
    },
    "classifier": {
        "source_address": "c2000000",
        "destination_address": "a0000000",
        "bytes": 13_008_016,
        "crc32": "fdc532c6",
    },
}
EXPECTED_COMPONENTS = {
    "frontend_sha256": "71bd258230cd7897a278132d973e0c9c5ea40a3d0be3ed3a3702175d387732d0",
    "source_backbone_sha256": "ee4f06403d86d8cf68687cbc362387825b05fa0550e56e8470db96f0827c7eac",
    "compiled_backbone_sha256": "72b80de6c7cf91ae4eb40036d399b4f792ce183d5f629b974f347edec01556c1",
    "gem_sha256": "ab45d405a4c0baca356acffd49e08bd3d4b5ec4d5daca48ce947455850d67353",
    "source_classifier_sha256": "7f862ab3db2cd5b72307d5c461de0ab8ed6fa3ce97ca93b335718e594e30ce81",
    "compiled_classifier_sha256": "2ff002cb4bf95b33384d70023a34e377b1d34bb2786e39efc5d358c3f80a6e93",
    "labels_sha256": "8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0",
}
DEFAULT_FIXTURE = Path("records/m1/fixtures/canonical_waveform.npy")


class ProtocolError(RuntimeError):
    pass


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def available_h1_ports() -> list[Any]:
    return [
        port
        for port in list_ports.comports()
        if port.vid == USB_VID and port.pid == USB_PID and port.serial_number == USB_SERIAL
    ]


def resolve_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    matches = available_h1_ports()
    if len(matches) != 1:
        identities = [
            {
                "device": port.device,
                "vid": port.vid,
                "pid": port.pid,
                "serial": port.serial_number,
                "product": port.product,
            }
            for port in matches
        ]
        raise RuntimeError(
            f"Expected exactly one {USB_VID:04x}:{USB_PID:04x}/{USB_SERIAL} CDC port; "
            f"found {len(matches)}: {identities}"
        )
    return matches[0].device


@dataclass(frozen=True)
class Reply:
    message_type: int
    sequence: int
    payload: bytes
    value: dict[str, Any]


class H1Client:
    def __init__(self, port: str | None, timeout: float) -> None:
        self.port = resolve_port(port)
        self.timeout = timeout
        self.sequence = 0
        self.serial = serial.Serial(
            self.port,
            baudrate=115200,
            timeout=min(timeout, 0.25),
            write_timeout=timeout,
            exclusive=True,
        )
        self.serial.dtr = True
        self.serial.reset_input_buffer()

    def close(self) -> None:
        self.serial.close()

    def __enter__(self) -> "H1Client":
        return self

    def __exit__(self, *_: Any) -> None:
        self.close()

    def _write_all(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            written = self.serial.write(view)
            if written <= 0:
                raise TimeoutError("CDC write made no progress")
            view = view[written:]
        self.serial.flush()

    def _read_exact(self, count: int, deadline: float) -> bytes:
        result = bytearray()
        while len(result) < count:
            if time.monotonic() >= deadline:
                raise TimeoutError(f"Timed out after receiving {len(result)}/{count} bytes")
            chunk = self.serial.read(count - len(result))
            if chunk:
                result.extend(chunk)
        return bytes(result)

    def _read_header(self, deadline: float) -> bytes:
        matched = 0
        while matched < len(MAGIC):
            byte = self._read_exact(1, deadline)[0]
            if byte == MAGIC[matched]:
                matched += 1
            else:
                matched = 1 if byte == MAGIC[0] else 0
        return MAGIC + self._read_exact(HEADER.size - len(MAGIC), deadline)

    def command(self, message_type: int, payload: bytes = b"", timeout: float | None = None) -> Reply:
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        sequence = self.sequence
        frame = HEADER.pack(
            MAGIC,
            PROTOCOL_VERSION,
            message_type,
            len(payload),
            sequence,
            crc32(payload),
        ) + payload
        self._write_all(frame)
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        header = self._read_header(deadline)
        magic, version, reply_type, length, reply_sequence, declared_crc = HEADER.unpack(header)
        if magic != MAGIC or version != PROTOCOL_VERSION:
            raise ProtocolError(f"Invalid reply header: magic={magic!r}, version={version}")
        if reply_sequence != sequence:
            raise ProtocolError(f"Reply sequence {reply_sequence} != request {sequence}")
        reply_payload = self._read_exact(length, deadline)
        actual_crc = crc32(reply_payload)
        if actual_crc != declared_crc:
            raise ProtocolError(
                f"Reply CRC mismatch: actual={actual_crc:08x}, declared={declared_crc:08x}"
            )
        try:
            value = json.loads(reply_payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ProtocolError("Reply payload is not valid UTF-8 JSON") from error
        if reply_type == ERROR_TYPE or not value.get("ok", False):
            raise ProtocolError(f"Board rejected command {message_type}: {value}")
        expected_type = message_type | RESPONSE_BIT
        if reply_type != expected_type:
            raise ProtocolError(f"Reply type {reply_type:#x} != expected {expected_type:#x}")
        return Reply(reply_type, reply_sequence, reply_payload, value)


def load_npy_float32(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 10 or data[:6] != b"\x93NUMPY":
        raise ValueError(f"{path} is not an NPY file")
    major, minor = data[6], data[7]
    if major == 1:
        header_length = int.from_bytes(data[8:10], "little")
        header_offset = 10
    elif major in (2, 3):
        header_length = int.from_bytes(data[8:12], "little")
        header_offset = 12
    else:
        raise ValueError(f"Unsupported NPY version {major}.{minor}")
    header_end = header_offset + header_length
    try:
        metadata = ast.literal_eval(data[header_offset:header_end].decode("latin1").strip())
    except (SyntaxError, ValueError) as error:
        raise ValueError("Invalid NPY header") from error
    if metadata.get("descr") not in ("<f4", "=f4"):
        raise ValueError(f"Expected little-endian float32, got {metadata.get('descr')!r}")
    if metadata.get("fortran_order") is not False:
        raise ValueError("Fortran-order waveform is not accepted")
    if tuple(metadata.get("shape", ())) != (1, 96000):
        raise ValueError(f"Expected shape (1, 96000), got {metadata.get('shape')!r}")
    raw = data[header_end:]
    if len(raw) != 384000:
        raise ValueError(f"Expected 384000 raw bytes, got {len(raw)}")
    return raw


def upload_payload(raw: bytes) -> bytes:
    raw_sha = hashlib.sha256(raw).digest()
    metadata = struct.pack(
        "<7I32s",
        1,
        2,
        1,
        96000,
        32000,
        len(raw),
        crc32(raw),
        raw_sha,
    )
    assert len(metadata) == 60
    return metadata + raw


def request_json(client: H1Client, command: int, *, timeout: float | None = None) -> dict[str, Any]:
    return client.command(command, timeout=timeout).value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_identity(identity: dict[str, Any]) -> None:
    require(identity.get("scope") == "H1_ENGINEERING_DEVELOPMENT", "Scope boundary changed")
    require(identity.get("candidate") == "DEVELOPMENT", "Candidate boundary changed")
    require(identity.get("m4") == "BLOCKED", "M4 boundary changed")
    require(identity.get("canonical_m5_input") is None, "Unexpected canonical M5 input")
    require(identity.get("formal_m5_acceptance") is False, "Unexpected M5 acceptance claim")
    for key, expected in EXPECTED_COMPONENTS.items():
        require(identity.get(key) == expected, f"{key} identity mismatch")
    require(identity.get("sdk_alif_commit") == "a524855a8b470fff0e3edb2c902a034d45ddf2a4", "sdk-alif changed")
    require(identity.get("zephyr_commit") == "3a2b84d96961b53431a78685c6ec0f0df4ddf347", "Zephyr changed")
    require(identity.get("zephyr_sdk") == "0.17.0", "Zephyr SDK changed")
    require(identity.get("compiler") == "gcc-12.2.0", "Compiler changed")
    require(identity.get("ethos_u_driver") == "0.16.0", "Ethos-U driver changed")
    require(identity.get("vela") == "5.0.0", "Vela changed")
    require(identity.get("protocol_version") == PROTOCOL_VERSION, "Protocol changed")
    require(len(identity.get("firmware_source_bundle_sha256", "")) == 64, "Missing source bundle")


def validate_endpoint(topk: dict[str, Any], summary: dict[str, Any]) -> None:
    actual_top10 = [entry["index"] for entry in topk["top_k"]]
    require(actual_top10 == EXPECTED_TOP10, f"Top-10 regression: {actual_top10}")
    require(topk.get("score_crc32") == EXPECTED_SCORE_CRC32, "Top-k score CRC regression")
    require(summary.get("score_crc32") == EXPECTED_SCORE_CRC32, "Summary score CRC regression")
    require(summary.get("input_sha256") == CANONICAL_RAW_SHA256, "Fixture SHA mismatch")
    require(summary.get("input_crc32") == f"{CANONICAL_RAW_CRC32:08x}", "Fixture CRC mismatch")
    require(summary.get("fixture_identity") == "canonical_synthetic", "Fixture identity mismatch")
    require(summary.get("finite_count") == 11560, "Non-finite endpoint score")
    require(summary.get("threshold_bits") == "3e19999a", "Threshold identity changed")
    require(summary.get("threshold_count") == 0, "Reporting decision changed")
    require(
        summary.get("boundary_crc32") == EXPECTED_BOUNDARY_CRC32,
        f"Boundary CRC regression: {summary.get('boundary_crc32')}",
    )


PROFILE_FIELDS = (
    "frontend_to_backbone_invoke_us",
    "frontend_m55_us",
    "backbone_submit_to_irq_us",
    "backbone_invoke_us",
    "backbone_to_gem_handoff_us",
    "gem_m55_us",
    "gem_to_classifier_handoff_us",
    "classifier_submit_to_irq_us",
    "classifier_invoke_us",
    "postprocess_m55_us",
    "total_compute_us",
)


def cycles_to_us(cycles: int, clock_hz: int) -> int:
    return (cycles * 1_000_000 + clock_hz // 2) // clock_hz


def validate_measurement(value: dict[str, Any], name: str, clock_hz: int) -> None:
    require(value.get("count") == 1, f"{name} occurrence count is not one")
    cycles = value.get("cycles")
    microseconds = value.get("us")
    require(isinstance(cycles, int) and cycles > 0, f"Invalid {name} cycles")
    require(isinstance(microseconds, int) and microseconds >= 0, f"Invalid {name} us")
    require(
        microseconds == cycles_to_us(cycles, clock_hz),
        f"{name} cycle-to-microsecond conversion mismatch",
    )


def validate_lifecycle(profile: dict[str, Any], model: str, clock_hz: int) -> None:
    expected = MODEL_EXPECTATIONS[model]
    lifecycle = profile.get(f"{model}_lifecycle")
    require(isinstance(lifecycle, dict), f"Missing {model} lifecycle")
    require(
        lifecycle.get("model_source_address") == expected["source_address"],
        f"{model} source address changed",
    )
    require(
        lifecycle.get("model_destination_address") == expected["destination_address"],
        f"{model} destination address changed",
    )
    require(lifecycle.get("model_bytes") == expected["bytes"], f"{model} size changed")
    require(
        lifecycle.get("model_source_crc32") == expected["crc32"],
        f"{model} source CRC mismatch",
    )
    require(
        lifecycle.get("model_destination_crc32") == expected["crc32"],
        f"{model} destination CRC mismatch",
    )
    require(lifecycle.get("model_memcmp_result") == 0, f"{model} memcmp mismatch")
    require(lifecycle.get("arena_used_bytes", 0) > 0, f"Missing {model} arena use")
    require(lifecycle.get("input_bytes", 0) > 0, f"Missing {model} input size")
    require(lifecycle.get("output_bytes", 0) > 0, f"Missing {model} output size")

    start = lifecycle.get("lifecycle_start_cycles")
    end = lifecycle.get("lifecycle_end_cycles")
    require(
        isinstance(start, int) and isinstance(end, int) and start < end,
        f"Invalid {model} lifecycle bounds",
    )
    lifecycle_measurement = lifecycle.get("model_lifecycle")
    require(isinstance(lifecycle_measurement, dict), f"Missing {model} lifecycle total")
    require(
        lifecycle_measurement.get("cycles") == end - start,
        f"{model} lifecycle total does not match its bounds",
    )
    require(
        lifecycle_measurement.get("us") == cycles_to_us(end - start, clock_hz),
        f"{model} lifecycle total conversion mismatch",
    )

    for key in (
        "source_crc",
        "copy",
        "destination_crc",
        "memcmp_verify",
        "flatbuffer_validate",
        "runtime_init",
        "allocate_tensors",
        "tensor_bind",
        "input_copy",
        "output_copy",
    ):
        measurement = lifecycle.get(key)
        require(isinstance(measurement, dict), f"Missing {model} {key}")
        validate_measurement(measurement, f"{model} {key}", clock_hz)

    crc_total = lifecycle.get("crc_total")
    require(isinstance(crc_total, dict), f"Missing {model} CRC total")
    crc_cycles = lifecycle["source_crc"]["cycles"] + lifecycle["destination_crc"]["cycles"]
    require(crc_total.get("cycles") == crc_cycles, f"{model} CRC total mismatch")
    require(
        crc_total.get("us") == cycles_to_us(crc_cycles, clock_hz),
        f"{model} CRC total conversion mismatch",
    )

    cache = lifecycle.get("cache_prepare")
    require(isinstance(cache, dict), f"Missing {model} cache preparation")
    validate_measurement(cache, f"{model} cache_prepare", clock_hz)
    cache_start = cache.get("start_cycles")
    cache_end = cache.get("end_cycles")
    require(
        isinstance(cache_start, int)
        and isinstance(cache_end, int)
        and cache_start < cache_end
        and cache["cycles"] == cache_end - cache_start,
        f"Invalid {model} cache preparation bounds",
    )
    require(
        isinstance(cache.get("address"), str) and len(cache["address"]) == 8,
        f"Invalid {model} cache address",
    )
    require(
        isinstance(cache.get("bytes"), int) and cache["bytes"] > 0,
        f"Invalid {model} cache bytes",
    )


def validate_profile(profile: dict[str, Any]) -> None:
    require(profile.get("profile_version") == 2, "Profile version mismatch")
    require(profile.get("valid") is True, "Profile is invalid")
    require(profile.get("clock_hz") == 400_000_000, "Unexpected timing clock")
    require(profile.get("transport_excluded") is True, "Transport exclusion not asserted")
    require(profile.get("unassociated_command_count") == 0, "Unassociated NPU command")
    require(profile.get("unassociated_irq_count") == 0, "Unassociated NPU IRQ")
    require(
        profile.get("unassociated_cache_prepare_count") == 0,
        "Unassociated cache preparation",
    )
    for field in PROFILE_FIELDS:
        require(isinstance(profile.get(field), int) and profile[field] > 0, f"Invalid {field}")
    require(
        isinstance(profile.get("pre_frontend_overhead_cycles"), int)
        and profile["pre_frontend_overhead_cycles"] >= 0,
        "Invalid pre-frontend overhead cycles",
    )
    require(
        profile.get("pre_frontend_overhead_us")
        == cycles_to_us(profile["pre_frontend_overhead_cycles"], profile["clock_hz"]),
        "Pre-frontend conversion mismatch",
    )
    require(
        profile["backbone_a_cycles"]
        < profile["backbone_b_cycles"]
        < profile["backbone_c_cycles"]
        < profile["backbone_d_cycles"],
        "Backbone A/B/C/D ordering failed",
    )
    require(
        profile["classifier_a_cycles"]
        < profile["classifier_b_cycles"]
        < profile["classifier_c_cycles"]
        < profile["classifier_d_cycles"],
        "Classifier A/B/C/D ordering failed",
    )
    validate_lifecycle(profile, "backbone", profile["clock_hz"])
    validate_lifecycle(profile, "classifier", profile["clock_hz"])
    require(
        profile["backbone_lifecycle"]["lifecycle_end_cycles"]
        == profile["backbone_a_cycles"],
        "Backbone lifecycle does not end at Invoke A",
    )
    require(
        profile["classifier_lifecycle"]["lifecycle_end_cycles"]
        == profile["classifier_a_cycles"],
        "Classifier lifecycle does not end at Invoke A",
    )


def model_events(profile: dict[str, Any], inference_index: int) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for model in ("backbone", "classifier"):
        lifecycle = profile[f"{model}_lifecycle"]
        common = {"inference_index": inference_index, "model": model}

        def add(
            action: str,
            measurement: dict[str, Any],
            detail: str,
            *,
            source: str | None = None,
            destination: str | None = None,
            byte_count: int | None = None,
        ) -> None:
            events.append(
                {
                    **common,
                    "action": action,
                    "pass_or_detail": detail,
                    "source_address": source,
                    "destination_address": destination,
                    "bytes": byte_count,
                    "cycles": measurement["cycles"],
                    "microseconds": measurement["us"],
                }
            )

        add(
            "CRC_VERIFY",
            lifecycle["source_crc"],
            "OSPI_SOURCE_PASS",
            source=lifecycle["model_source_address"],
            byte_count=lifecycle["model_bytes"],
        )
        add(
            "COPY",
            lifecycle["copy"],
            "OSPI_TO_PSRAM",
            source=lifecycle["model_source_address"],
            destination=lifecycle["model_destination_address"],
            byte_count=lifecycle["model_bytes"],
        )
        add(
            "CRC_VERIFY",
            lifecycle["destination_crc"],
            "PSRAM_DESTINATION_PASS",
            source=lifecycle["model_destination_address"],
            byte_count=lifecycle["model_bytes"],
        )
        add(
            "MEMCMP_VERIFY",
            lifecycle["memcmp_verify"],
            "OSPI_VS_PSRAM_FULL_PAYLOAD",
            source=lifecycle["model_source_address"],
            destination=lifecycle["model_destination_address"],
            byte_count=lifecycle["model_bytes"],
        )
        add(
            "FLATBUFFER_VALIDATE",
            lifecycle["flatbuffer_validate"],
            "MODEL_AND_OPERATOR_CONTRACT",
        )
        add(
            "RUNTIME_INIT",
            lifecycle["runtime_init"],
            "RESOLVER_ADD_ETHOSU_INTERPRETER_CONSTRUCT",
        )
        add(
            "ALLOCATE_TENSORS",
            lifecycle["allocate_tensors"],
            "TFLM_ALLOCATE_TENSORS",
        )
        add(
            "TENSOR_BIND",
            lifecycle["tensor_bind"],
            "APPLICATION_TENSOR_RETRIEVE_AND_VALIDATE",
        )
        add(
            "INPUT_COPY",
            lifecycle["input_copy"],
            "APPLICATION_INPUT_TO_TFLM_TENSOR",
            byte_count=lifecycle["input_bytes"],
        )
        cache = lifecycle["cache_prepare"]
        add(
            "CACHE_PREPARE",
            cache,
            "ETHOSU_DRIVER_BASE_POINTER_FLUSH",
            destination=cache["address"],
            byte_count=cache["bytes"],
        )
        add(
            "INVOKE",
            {
                "cycles": profile[f"{model}_invoke_cycles"],
                "us": profile[f"{model}_invoke_us"],
            },
            "TFLM_INVOKE_A_TO_D",
        )
        add(
            "OUTPUT_COPY",
            lifecycle["output_copy"],
            "TFLM_TENSOR_TO_APPLICATION_OUTPUT",
            byte_count=lifecycle["output_bytes"],
        )
    return events


def throughput_mib_s(byte_count: int, microseconds: int) -> float:
    require(microseconds > 0, "Cannot calculate throughput from zero microseconds")
    return byte_count * 1_000_000.0 / (1_048_576.0 * microseconds)


def lifecycle_throughput(profile: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for model in ("backbone", "classifier"):
        lifecycle = profile[f"{model}_lifecycle"]
        byte_count = lifecycle["model_bytes"]
        result[model] = {
            "bytes": byte_count,
            "copy_mib_s": throughput_mib_s(byte_count, lifecycle["copy"]["us"]),
            "source_crc_mib_s": throughput_mib_s(byte_count, lifecycle["source_crc"]["us"]),
            "destination_crc_mib_s": throughput_mib_s(
                byte_count, lifecycle["destination_crc"]["us"]
            ),
        }
    return result


def handoff_decomposition(profile: dict[str, Any], model: str) -> dict[str, Any]:
    clock_hz = profile["clock_hz"]
    lifecycle = profile[f"{model}_lifecycle"]
    if model == "backbone":
        broad_name = "frontend_to_backbone_invoke"
        quantize_name = "frontend_to_backbone_quantize"
    else:
        broad_name = "gem_to_classifier_handoff"
        quantize_name = "embedding_to_classifier_quantize"
    broad_cycles = profile[f"{broad_name}_cycles"]
    component_cycles = {
        "quantization": profile[f"{quantize_name}_cycles"],
        "source_crc": lifecycle["source_crc"]["cycles"],
        "copy": lifecycle["copy"]["cycles"],
        "destination_crc": lifecycle["destination_crc"]["cycles"],
        "memcmp_verify": lifecycle["memcmp_verify"]["cycles"],
        "flatbuffer_validate": lifecycle["flatbuffer_validate"]["cycles"],
        "runtime_init": lifecycle["runtime_init"]["cycles"],
        "allocate_tensors": lifecycle["allocate_tensors"]["cycles"],
        "tensor_bind": lifecycle["tensor_bind"]["cycles"],
        "input_copy": lifecycle["input_copy"]["cycles"],
    }
    lifecycle_measured = sum(
        value for key, value in component_cycles.items() if key != "quantization"
    )
    lifecycle_other = lifecycle["model_lifecycle"]["cycles"] - lifecycle_measured
    outside_lifecycle_other = (
        broad_cycles
        - component_cycles["quantization"]
        - lifecycle["model_lifecycle"]["cycles"]
    )
    require(lifecycle_other >= 0, f"Negative {model} lifecycle residual")
    require(outside_lifecycle_other >= 0, f"Negative {model} handoff residual")
    component_cycles["other"] = lifecycle_other + outside_lifecycle_other
    component_cycles["cache_prepare_inside_interval"] = 0
    require(
        sum(component_cycles.values()) == broad_cycles,
        f"{model} handoff accounting mismatch",
    )
    return {
        "interval": broad_name,
        "cycles": broad_cycles,
        "microseconds": profile[f"{broad_name}_us"],
        "components": {
            name: {"cycles": cycles, "microseconds": cycles_to_us(cycles, clock_hz)}
            for name, cycles in component_cycles.items()
        },
        "cache_boundary_note": (
            "Cache preparation occurs after Invoke A and is inside Invoke, not this interval."
        ),
        "cycle_accounting_residual": broad_cycles - sum(component_cycles.values()),
    }


def total_compute_decomposition(profile: dict[str, Any]) -> dict[str, Any]:
    names = (
        "pre_frontend_overhead",
        "frontend_m55",
        "frontend_to_backbone_invoke",
        "backbone_invoke",
        "backbone_to_gem_handoff",
        "gem_m55",
        "gem_to_classifier_handoff",
        "classifier_invoke",
        "classifier_to_postprocess_handoff",
        "postprocess_m55",
    )
    cycles = {name: profile[f"{name}_cycles"] for name in names}
    residual = profile["total_compute_cycles"] - sum(cycles.values())
    require(residual == 0, f"Total compute cycle accounting residual is {residual}")
    return {
        "total_compute_cycles": profile["total_compute_cycles"],
        "total_compute_us": profile["total_compute_us"],
        "components": {
            name: {
                "cycles": value,
                "microseconds": cycles_to_us(value, profile["clock_hz"]),
            }
            for name, value in cycles.items()
        },
        "cycle_accounting_residual": residual,
    }

def validate_canonical(client: H1Client, fixture: Path) -> dict[str, Any]:
    started = time.time()
    ping = request_json(client, PING)
    initial_status = request_json(client, STATUS)
    identity = request_json(client, GET_IDENTITY)
    validate_identity(identity)
    require(initial_status.get("compute_ready") is True, "Board compute path is not ready")

    raw = load_npy_float32(fixture)
    require(hashlib.sha256(raw).hexdigest() == CANONICAL_RAW_SHA256, "Host fixture SHA mismatch")
    require(crc32(raw) == CANONICAL_RAW_CRC32, "Host fixture CRC mismatch")
    upload = client.command(UPLOAD_WAVEFORM, upload_payload(raw), timeout=60.0).value
    require(upload.get("raw_bytes") == 384000, "Board upload length mismatch")
    require(upload.get("raw_crc32") == f"{CANONICAL_RAW_CRC32:08x}", "Board raw CRC mismatch")
    require(upload.get("declared_raw_sha256") == CANONICAL_RAW_SHA256, "Board SHA echo mismatch")
    require(upload.get("canonical_byte_match") is True, "Board canonical byte comparison failed")

    runs: list[dict[str, Any]] = []
    events: list[dict[str, Any]] = []
    for inference_index in range(1, 3):
        run = request_json(client, RUN_UPLOADED_WAVEFORM, timeout=180.0)
        topk = request_json(client, GET_TOPK)
        profile = request_json(client, GET_PROFILE)
        summary = request_json(client, GET_RESULT_SUMMARY)
        validate_endpoint(topk, summary)
        validate_profile(profile)
        require(run.get("score_crc32") == EXPECTED_SCORE_CRC32, "RUN score CRC regression")
        run_sequence = run.get("run_sequence")
        require(run_sequence == profile.get("run_sequence"), "Profile run sequence mismatch")
        require(run_sequence == topk.get("run_sequence"), "Top-k run sequence mismatch")
        require(run_sequence == summary.get("run_sequence"), "Summary run sequence mismatch")
        run_events = model_events(profile, inference_index)
        events.extend(run_events)
        runs.append(
            {
                "inference_index": inference_index,
                "run": run,
                "topk": topk,
                "profile": profile,
                "summary": summary,
                "model_events": run_events,
                "throughput_mib_s": lifecycle_throughput(profile),
                "handoff_decomposition": {
                    "before_backbone": handoff_decomposition(profile, "backbone"),
                    "gem_to_classifier": handoff_decomposition(profile, "classifier"),
                },
                "total_compute_decomposition": total_compute_decomposition(profile),
            }
        )

    require(
        runs[1]["run"]["run_sequence"] == runs[0]["run"]["run_sequence"] + 1,
        "Two inferences were not consecutive",
    )
    require(runs[1]["run"].get("repeat_comparable") is True, "Second run not compared")
    require(runs[1]["run"].get("repeat_equal") is True, "Board endpoint repeatability failed")
    require(runs[1]["summary"].get("repeat_equal") is True, "Summary repeatability failed")
    require(runs[0]["topk"]["top_k"] == runs[1]["topk"]["top_k"], "Top-k not repeatable")
    require(
        runs[0]["summary"]["boundary_crc32"] == runs[1]["summary"]["boundary_crc32"],
        "One or more board boundaries are not byte-repeatable",
    )

    timing: dict[str, Any] = {}
    for field in PROFILE_FIELDS:
        values = [run["profile"][field] for run in runs]
        timing[field] = {
            "runs": values,
            "min": min(values),
            "max": max(values),
            "range": max(values) - min(values),
            "mean": statistics.fmean(values),
        }
    final_status = request_json(client, STATUS)
    require(
        final_status.get("connection_generation") == initial_status.get("connection_generation"),
        "USB connection generation changed during the two-inference test",
    )
    return {
        "schema": "birdnet-v3-pre-m5-h1-usb-client-validation",
        "version": 1,
        "classification": "CLIENT_VALIDATION_PASS",
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "started_unix": started,
        "completed_unix": time.time(),
        "protocol_version": PROTOCOL_VERSION,
        "usb_identity": {
            "vid": f"{USB_VID:04x}",
            "pid": f"{USB_PID:04x}",
            "serial": USB_SERIAL,
        },
        "ping": ping,
        "initial_status": initial_status,
        "identity": identity,
        "upload": upload,
        "runs": runs,
        "model_events": events,
        "timing_summary_us": timing,
        "final_status": final_status,
        "checks": {
            "canonical_raw_bytes": len(raw),
            "canonical_raw_sha256": hashlib.sha256(raw).hexdigest(),
            "canonical_raw_crc32": f"{crc32(raw):08x}",
            "top1_top5_top10_equal": True,
            "score_crc32_equal": True,
            "reporting_decisions_equal": True,
            "all_boundary_crc32_repeat_equal": True,
            "transport_excluded": True,
            "single_client_serial_context": True,
            "single_upload": True,
            "two_consecutive_inferences": True,
            "connection_generation_unchanged": True,
            "no_reboot_between_inferences": True,
            "no_usb_disconnect_between_inferences": True,
            "no_model_reprogramming_between_inferences": True,
            "no_firmware_restart_between_inferences": True,
            "both_models_copy_count_each_inference": True,
            "both_models_source_crc_count_each_inference": True,
            "both_models_destination_crc_count_each_inference": True,
            "both_models_runtime_init_count_each_inference": True,
            "both_models_allocate_tensors_count_each_inference": True,
            "both_models_tensor_bind_count_each_inference": True,
            "both_models_cache_prepare_count_each_inference": True,
        },
    }


def wait_reconnect(port: str | None, timeout: float) -> dict[str, Any]:
    with H1Client(port, timeout) as client:
        before = request_json(client, PING)
        before_status = request_json(client, STATUS)
    print("Disconnect the native E8 USB-C cable now.", file=sys.stderr, flush=True)
    deadline = time.monotonic() + timeout
    while available_h1_ports() and time.monotonic() < deadline:
        time.sleep(0.1)
    require(not available_h1_ports(), "CDC device did not disappear before timeout")
    print("Reconnect the native E8 USB-C cable now.", file=sys.stderr, flush=True)
    while not available_h1_ports() and time.monotonic() < deadline:
        time.sleep(0.1)
    require(available_h1_ports(), "CDC device did not re-enumerate before timeout")
    with H1Client(None, timeout) as client:
        after = request_json(client, PING)
        after_status = request_json(client, STATUS)
    return {
        "classification": "RECONNECT_PASS",
        "before_ping": before,
        "before_status": before_status,
        "after_ping": after,
        "after_status": after_status,
        "generation_advanced": after_status["connection_generation"]
        > before_status["connection_generation"],
    }


def print_json(value: Any, output: Path | None) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if output is None:
        sys.stdout.write(encoded)
    else:
        output.write_text(encoded, encoding="utf-8")
        print(output)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CDC device path; default discovers fixed VID/PID/serial")
    parser.add_argument("--timeout", type=float, default=10.0, help="Per-command timeout in seconds")
    parser.add_argument("--output", type=Path, help="Write JSON response/evidence to this path")
    subparsers = parser.add_subparsers(dest="action", required=True)
    for action in ("ping", "status", "identity", "run-uploaded", "run-canonical", "topk", "profile", "summary"):
        subparsers.add_parser(action)
    upload_parser = subparsers.add_parser("upload")
    upload_parser.add_argument("fixture", type=Path, nargs="?", default=DEFAULT_FIXTURE)
    validate_parser = subparsers.add_parser("validate-canonical")
    validate_parser.add_argument("fixture", type=Path, nargs="?", default=DEFAULT_FIXTURE)
    reconnect_parser = subparsers.add_parser("wait-reconnect")
    reconnect_parser.add_argument("--reconnect-timeout", type=float, default=120.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.action == "wait-reconnect":
        print_json(wait_reconnect(args.port, args.reconnect_timeout), args.output)
        return 0
    with H1Client(args.port, args.timeout) as client:
        if args.action == "validate-canonical":
            value = validate_canonical(client, args.fixture)
        elif args.action == "upload":
            raw = load_npy_float32(args.fixture)
            value = client.command(UPLOAD_WAVEFORM, upload_payload(raw), timeout=60.0).value
        else:
            command = {
                "ping": PING,
                "status": STATUS,
                "identity": GET_IDENTITY,
                "run-uploaded": RUN_UPLOADED_WAVEFORM,
                "run-canonical": RUN_CANONICAL,
                "topk": GET_TOPK,
                "profile": GET_PROFILE,
                "summary": GET_RESULT_SUMMARY,
            }[args.action]
            command_timeout = 180.0 if args.action.startswith("run-") else None
            value = request_json(client, command, timeout=command_timeout)
    print_json(value, args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ProtocolError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
