#!/usr/bin/env python3
"""Shared constants and byte identities for the prospective N1 M4 freeze.

This module is deliberately construction-neutral: importing it cannot emit a
TFLite model or execute a candidate.  Production construction remains behind
the explicit guard in :mod:`m4_n1_compose`.
"""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Iterable

import numpy as np


FREEZE_PARENT = "6e6050ad2afc26f8b63042b688af323b0dcd1673"
BLOCKED_FREEZE_COMMIT = "aec4da56a53b3c62392d26602d31c7d5ddefe4a0"
REPLACEMENT_FREEZE_BRANCH = "m4-n1-classifier-range-freeze"
GOVERNANCE_HEAD = FREEZE_PARENT
ACCEPTED_MAIN = "23de5003abe70cf90d3750c2f487af50c0241884"
SEALED_GEM_STUDY = "0018a73f1dc571480cba406e096d0499ce943e6c"
SEALED_SE_MEAN_STUDY = "090ab61dd3ba8d6703f74757417fa636461cd62b"

M3_SAVED_MODEL = Path(".m3-work/canonical-h1-savedmodel")
M3_TREE_SHA256 = "4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a"
M2_STABLEHLO_SHA256 = "3cff5699feedb7cde9c877f88ecaefc9c73da44fb735875411f5fdd10f78ecda"
LABELS = Path(".m1-work/official/BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv")
LABELS_SHA256 = "8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0"

GEM_PATH = Path("work/m4-componentized/candidate-01/gem.tflite")
GEM_BYTES = 1780
GEM_SHA256 = "ab45d405a4c0baca356acffd49e08bd3d4b5ec4d5daca48ce947455850d67353"
GEM_EPSILON_BITS = 0x358637BD
GEM_P_BITS = 0x40ED0F08
GEM_INVERSE_P_BITS = 0x3E0A3A34

SE_MEAN_RECOMMENDATION = Path("records/m4/se-mean-implementation-study/RECOMMENDATION.json")
SE_MEAN_RECOMMENDATION_SHA256 = "d6e1926abe576b20f046f4a8b0b8cb3b94c8060c8cb8f2e8af8f26b407c48dd5"
SE_MEAN_STUDY_SOURCE = Path("scripts/m4_se_mean_implementation_study.py")
SE_MEAN_STUDY_SOURCE_SHA256 = "5d8865c634532356f7d6084be66f28863692c7db49213dd6f1c4d60c903fd140"

CALIBRATION_SEED = 20260920
CALIBRATION_COUNT = 128
CALIBRATION_AGGREGATE_SHA256 = "b6566b5dc60340ca1376098db45eb461fbd3884b788506baf78214fcb634fff4"
PRIOR_QUALIFICATION_SEED = 20260921
PRIOR_QUALIFICATION_COUNT = 32
PRIOR_QUALIFICATION_AGGREGATE_SHA256 = "fb9e86dd00bc5667777c9c2a9db4cb0ab88e7ad35aad71e3b8d9920bd8b2c900"
QUALIFICATION_DOMAIN = "birdnet-v3-clean|next-m4-qualification|"
QUALIFICATION_COUNT = 32

REPLACEMENT_QUALIFICATION_NAMESPACE = "birdnet-v3-clean|m4-n1-classifier-range-freeze|"
REPLACEMENT_QUALIFICATION_TAG = "qualification-v2"
CLASSIFIER_INPUT_SCALE_BITS = 0x380049C8
CLASSIFIER_RANGE_ANCHOR_BITS = 0x3F8048C7
CLASSIFIER_INPUT_ZERO_POINT = 0
INT16_MIN = -32768
INT16_MAX = 32767

REGRESSION_RAW_SHA256 = {
    "canonical": "23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8",
    "silence": "fec9afb531a8e036eba1d81651896e1b2c1f78b0234dbadc8e7549563f09407b",
    "low_amplitude_sine": "0cc3df62eea9ab1a3f5e67c3b4a08bcb4cddc6557a8dbda332b7470fa0472e4e",
    "high_valid_two_tone": "1af7db58ba98f1850049411d6221673d9c3396fffe3a87de93dfd19d22f2a506",
    "structured_impulse": "94a56ab616a013aef6ca6dab022323fb9c8fa74e8a0235b7ce1079fe7c5d5296",
}
HISTORICAL_WITNESS_NAME = "historical_witness_generated_0016"
HISTORICAL_WITNESS_RAW_SHA256 = "2079a69407080a56faa1c60121e50fa4b398e4b6dc42acb8503bf736659e60a5"


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def file_identity(path: Path) -> dict[str, Any]:
    payload = path.read_bytes()
    return {"path": path.as_posix(), "bytes": len(payload), "sha256": sha256_bytes(payload)}


def array_sha256(value: Any) -> str:
    return sha256_bytes(np.ascontiguousarray(np.asarray(value)).tobytes())


def array_identity(value: Any) -> dict[str, Any]:
    array = np.asarray(value)
    return {
        "shape": [int(item) for item in array.shape],
        "dtype": str(array.dtype),
        "elements": int(array.size),
        "raw_sha256": array_sha256(array),
        "finite": bool(np.isfinite(array).all()),
        "minimum": float(np.min(array, initial=np.inf)),
        "maximum": float(np.max(array, initial=-np.inf)),
    }


def rows_manifest(seed: int | str, rows: Iterable[np.ndarray]) -> dict[str, Any]:
    digest = hashlib.sha256()
    entries = []
    for index, row in enumerate(rows):
        identity = array_sha256(row)
        digest.update(f"{index:04d} {identity}\n".encode())
        entries.append({"index": index, "raw_sha256": identity})
    return {
        "seed": seed,
        "count": len(entries),
        "aggregate_algorithm": "sha256 of ordered '<index:04d> <raw_sha256>\\n' rows",
        "aggregate_sha256": digest.hexdigest(),
        "rows": entries,
    }


def named_rows_manifest(rows: dict[str, np.ndarray]) -> dict[str, Any]:
    digest = hashlib.sha256()
    entries: dict[str, Any] = {}
    for name in sorted(rows):
        identity = array_identity(rows[name])
        digest.update(f"{name} {identity['raw_sha256']}\n".encode())
        entries[name] = identity
    return {
        "count": len(entries),
        "aggregate_algorithm": "sha256 of sorted '<name> <raw_sha256>\\n' rows",
        "aggregate_sha256": digest.hexdigest(),
        "rows": entries,
    }


def float32_from_bits(bits: int) -> np.float32:
    return np.frombuffer(struct.pack("<I", bits), dtype="<f4")[0]


def float32_bits(value: Any) -> str:
    bits = struct.unpack("<I", np.asarray([value], dtype="<f4").tobytes())[0]
    return f"0x{bits:08x}"


def derived_qualification_seed(parent: str = FREEZE_PARENT) -> dict[str, Any]:
    if len(parent) != 40 or parent != parent.lower():
        raise RuntimeError("freeze parent must be lowercase 40-hex")
    payload = (QUALIFICATION_DOMAIN + parent).encode("utf-8")
    digest = hashlib.sha256(payload).digest()
    seed_bytes = digest[:16]
    return {
        "domain_separator": QUALIFICATION_DOMAIN,
        "freeze_parent": parent,
        "input_utf8_sha256": digest.hex(),
        "seed_rule": "first 128 SHA-256 bits interpreted big-endian",
        "seed_hex": seed_bytes.hex(),
        "seed_decimal": int.from_bytes(seed_bytes, "big"),
    }


def derived_replacement_qualification_seed(
    base: str = BLOCKED_FREEZE_COMMIT,
) -> dict[str, Any]:
    if len(base) != 40 or base != base.lower():
        raise RuntimeError("replacement freeze base must be lowercase 40-hex")
    material = REPLACEMENT_QUALIFICATION_NAMESPACE + base + "|" + REPLACEMENT_QUALIFICATION_TAG
    payload = material.encode("utf-8")
    digest = hashlib.sha256(payload).digest()
    seed_bytes = digest[:16]
    return {
        "namespace": REPLACEMENT_QUALIFICATION_NAMESPACE,
        "replacement_range_freeze_base": base,
        "tag": REPLACEMENT_QUALIFICATION_TAG,
        "seed_material_utf8": material,
        "sha256": digest.hex(),
        "selected_prefix": seed_bytes.hex(),
        "selected_prefix_bits": 128,
        "byte_order": "big-endian",
        "integer_seed": int.from_bytes(seed_bytes, "big"),
    }


def require_file(path: Path, expected_sha256: str, expected_bytes: int | None = None) -> dict[str, Any]:
    identity = file_identity(path)
    if identity["sha256"] != expected_sha256:
        raise RuntimeError(f"identity mismatch for {path}: {identity['sha256']} != {expected_sha256}")
    if expected_bytes is not None and identity["bytes"] != expected_bytes:
        raise RuntimeError(f"size mismatch for {path}: {identity['bytes']} != {expected_bytes}")
    return identity


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
