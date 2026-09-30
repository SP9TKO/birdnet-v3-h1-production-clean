#!/usr/bin/env python3
"""Reproduce the frozen Pre-M5 biological waveform fixtures.

The prospective source-frame intervals are immutable inputs. This tool verifies
the committed freeze and every original, decodes with the frozen GStreamer
stack, applies the documented mono rule, resamples to 32 kHz, and writes
ignored float32 payloads plus a compact local result. It never runs BirdNET.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import UTC, datetime
from pathlib import Path

import numpy as np


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_MANIFEST = (
    REPO_ROOT / "records" / "development" / "BIOLOGICAL_AUDIO_SOURCES.json"
)
DEFAULT_WINDOW_MANIFEST = (
    REPO_ROOT / "records" / "development" / "BIOLOGICAL_AUDIO_WINDOWS.json"
)
DEFAULT_ORIGINALS = (
    REPO_ROOT / ".development-work" / "biological-audio" / "originals"
)
DEFAULT_ACQUISITION_LOCK = (
    REPO_ROOT
    / ".development-work"
    / "biological-audio"
    / "BIOLOGICAL_AUDIO_LOCK.json"
)
DEFAULT_OUTPUT_DIR = (
    REPO_ROOT / ".development-work" / "biological-audio" / "windows"
)
DEFAULT_WORK_DIR = REPO_ROOT / ".development-work" / "biological-audio" / "prepare"
DEFAULT_RESULT = (
    REPO_ROOT
    / ".development-work"
    / "biological-audio"
    / "BIOLOGICAL_AUDIO_PREPARATION_RESULT.json"
)

EXPECTED_SOURCE_MANIFEST_SHA256 = (
    "397d559b7a43be26f1b1e362eaa45ce09d2d693f95aa00e3028d278f483ed1e6"
)
EXPECTED_WINDOW_MANIFEST_SHA256 = (
    "088c7aa191142339ed4b79cb6b49ea832a1568dc71b6a694aabba9949c5da6f4"
)
EXPECTED_PYTHON = (3, 12, 14)
EXPECTED_NUMPY = "2.3.5"
EXPECTED_TOOL_HASHES = {
    "/usr/bin/gst-launch-1.0": (
        "f5509cd51b3398b141417914d1317cb495fd0a6a393bf818db1f0146fcca4ac1"
    ),
    "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstogg.so": (
        "bef6163b86ce3399553771e15ca2ae10d3bb4652bc2e10b4587dcefdfab1c1f4"
    ),
    "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstvorbis.so": (
        "dd5055d577a46b63d017de9ad326e85e9208b765645a6fe32f395d8edfb6cb86"
    ),
    "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstrawparse.so": (
        "871f55c197b2de8b3f75e2334ca2100a4b9b3cb3b774c49f0be6235ca42363c0"
    ),
    "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstaudioresample.so": (
        "3ab3e6f37d24ea21e6e775fd7c5ab50988dd04799c68ff67cb388223283cd353"
    ),
}


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_equal(label: str, observed: object, expected: object) -> None:
    if observed != expected:
        raise RuntimeError(f"{label}: got {observed!r}, expected {expected!r}")


def verify_runtime_and_tools() -> dict[str, object]:
    require_equal("Python", sys.version_info[:3], EXPECTED_PYTHON)
    require_equal("NumPy", np.__version__, EXPECTED_NUMPY)
    observed_hashes: dict[str, str] = {}
    for path_text, expected_hash in EXPECTED_TOOL_HASHES.items():
        path = Path(path_text)
        observed_hash = sha256_file(path)
        require_equal(f"tool SHA-256 {path}", observed_hash, expected_hash)
        observed_hashes[path_text] = observed_hash
    version = subprocess.run(
        ["/usr/bin/gst-launch-1.0", "--version"],
        check=True,
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "C"},
    ).stdout
    if "GStreamer 1.28.2" not in version:
        raise RuntimeError(f"unexpected GStreamer version output: {version!r}")
    return {
        "python": ".".join(str(part) for part in sys.version_info[:3]),
        "python_executable": sys.executable,
        "python_executable_sha256": sha256_file(Path(sys.executable)),
        "numpy": np.__version__,
        "gstreamer": "1.28.2",
        "tool_hashes": observed_hashes,
    }


def run_gstreamer(arguments: list[str]) -> None:
    subprocess.run(
        ["/usr/bin/gst-launch-1.0", "-q", *arguments],
        check=True,
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "C"},
    )


def decode_original(
    source: Path,
    destination: Path,
    sample_rate: int,
    channels: int,
) -> None:
    run_gstreamer(
        [
            "filesrc",
            f"location={source}",
            "!",
            "oggdemux",
            "!",
            "vorbisdec",
            "!",
            (
                "audio/x-raw,format=F32LE,layout=interleaved,"
                f"rate={sample_rate},channels={channels}"
            ),
            "!",
            "filesink",
            f"location={destination}",
        ]
    )


def resample_mono(
    source: Path,
    destination: Path,
    sample_rate: int,
) -> None:
    run_gstreamer(
        [
            "filesrc",
            f"location={source}",
            "!",
            "rawaudioparse",
            "pcm-format=f32le",
            f"sample-rate={sample_rate}",
            "num-channels=1",
            "!",
            "audioresample",
            "resample-method=kaiser",
            "quality=10",
            "sinc-filter-mode=full",
            "sinc-filter-interpolation=none",
            "!",
            "audio/x-raw,format=F32LE,layout=interleaved,rate=32000,channels=1",
            "!",
            "filesink",
            f"location={destination}",
        ]
    )


def waveform_stats(values: np.ndarray) -> dict[str, object]:
    values64 = values.astype(np.float64)
    finite = np.isfinite(values)
    return {
        "elements": int(values.size),
        "bytes": int(values.nbytes),
        "finite_count": int(finite.sum()),
        "nonfinite_count": int((~finite).sum()),
        "minimum": float(values.min()),
        "maximum": float(values.max()),
        "mean": float(values64.mean()),
        "rms": float(np.sqrt(np.mean(values64 * values64))),
        "samples_abs_ge_1": int((np.abs(values) >= 1.0).sum()),
    }


def verify_candidate_stats(
    identifier: str,
    mono: np.ndarray,
    frozen: dict[str, object],
) -> None:
    stats = waveform_stats(mono)
    checks = {
        "candidate_finite_count_after_mono": stats["finite_count"],
        "candidate_minimum_after_mono": stats["minimum"],
        "candidate_maximum_after_mono": stats["maximum"],
        "candidate_rms_after_mono": stats["rms"],
        "candidate_samples_abs_ge_1_after_mono": stats["samples_abs_ge_1"],
    }
    for key, observed in checks.items():
        expected = frozen[key]
        if isinstance(observed, float):
            if not math.isclose(observed, expected, rel_tol=0.0, abs_tol=0.0):
                raise RuntimeError(
                    f"{identifier} {key}: got {observed!r}, expected {expected!r}"
                )
        else:
            require_equal(f"{identifier} {key}", observed, expected)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-manifest", type=Path, default=DEFAULT_SOURCE_MANIFEST
    )
    parser.add_argument(
        "--window-manifest", type=Path, default=DEFAULT_WINDOW_MANIFEST
    )
    parser.add_argument(
        "--acquisition-lock", type=Path, default=DEFAULT_ACQUISITION_LOCK
    )
    parser.add_argument("--originals-dir", type=Path, default=DEFAULT_ORIGINALS)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK_DIR)
    parser.add_argument("--result", type=Path, default=DEFAULT_RESULT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_bytes = args.source_manifest.read_bytes()
    window_bytes = args.window_manifest.read_bytes()
    require_equal(
        "source manifest SHA-256",
        sha256_bytes(source_bytes),
        EXPECTED_SOURCE_MANIFEST_SHA256,
    )
    require_equal(
        "window manifest SHA-256",
        sha256_bytes(window_bytes),
        EXPECTED_WINDOW_MANIFEST_SHA256,
    )
    sources = json.loads(source_bytes)
    windows = json.loads(window_bytes)
    require_equal(
        "window state",
        windows["state"],
        "PROSPECTIVE_WINDOWS_FROZEN_NO_MODEL_OUTPUT_CONSULTED",
    )
    require_equal(
        "model output consulted",
        windows["selection_protocol"]["model_output_consulted"],
        False,
    )
    require_equal("window count", len(windows["entries"]), 7)

    runtime = verify_runtime_and_tools()
    acquisition_lock = json.loads(args.acquisition_lock.read_text(encoding="utf-8"))
    require_equal("acquisition lock state", acquisition_lock["state"], "COMPLETE")
    require_equal(
        "acquisition source manifest SHA-256",
        acquisition_lock["source_manifest_sha256"],
        EXPECTED_SOURCE_MANIFEST_SHA256,
    )
    require_equal(
        "acquisition BirdNET inference",
        acquisition_lock["birdnet_inference_performed"],
        False,
    )

    source_by_id = {entry["id"]: entry for entry in sources["entries"]}
    lock_by_id = {entry["id"]: entry for entry in acquisition_lock["entries"]}
    require_equal("source IDs", set(source_by_id), set(lock_by_id))
    require_equal(
        "window IDs", {entry["id"] for entry in windows["entries"]}, set(source_by_id)
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.result.parent.mkdir(parents=True, exist_ok=True)
    result_entries: list[dict[str, object]] = []

    with tempfile.TemporaryDirectory(
        prefix="biological-windows-", dir=args.work_dir
    ) as temporary_directory:
        temporary_root = Path(temporary_directory)
        for frozen in windows["entries"]:
            identifier = frozen["id"]
            source_entry = source_by_id[identifier]
            lock_entry = lock_by_id[identifier]
            original = args.originals_dir / frozen["original_file"]
            require_equal(
                f"{identifier} original size",
                original.stat().st_size,
                source_entry["expected_size_bytes"],
            )
            require_equal(
                f"{identifier} original SHA-1",
                sha1_file(original),
                source_entry["expected_sha1"],
            )
            original_sha256 = sha256_file(original)
            require_equal(
                f"{identifier} original SHA-256",
                original_sha256,
                frozen["original_sha256"],
            )
            require_equal(
                f"{identifier} acquisition lock SHA-256",
                lock_entry["observed_sha256"],
                original_sha256,
            )

            media = frozen["media"]
            decoded_path = temporary_root / f"{identifier}.decoded.f32le"
            decode_original(
                original,
                decoded_path,
                media["sample_rate_hz"],
                media["channels"],
            )
            decoded_bytes = decoded_path.read_bytes()
            require_equal(
                f"{identifier} decoded bytes",
                len(decoded_bytes),
                media["decoded_bytes"],
            )
            require_equal(
                f"{identifier} decoded SHA-256",
                sha256_bytes(decoded_bytes),
                media["decoded_f32le_sha256"],
            )
            decoded = np.frombuffer(decoded_bytes, dtype="<f4")
            require_equal(
                f"{identifier} decoded scalar count",
                decoded.size,
                media["decoded_sample_frames"] * media["channels"],
            )
            decoded = decoded.reshape(media["decoded_sample_frames"], media["channels"])
            if not np.isfinite(decoded).all():
                raise RuntimeError(f"{identifier}: nonfinite decoded sample")

            window = frozen["window"]
            start = window["start_sample_frame"]
            end = window["end_sample_frame_exclusive"]
            selected = decoded[start:end]
            require_equal(
                f"{identifier} selected frame count",
                selected.shape[0],
                window["source_sample_frames"],
            )
            require_equal(
                f"{identifier} exact native three seconds",
                selected.shape[0],
                3 * media["sample_rate_hz"],
            )
            if media["channels"] == 1:
                mono = np.ascontiguousarray(selected[:, 0], dtype="<f4")
                channel_rule = "single channel preserved bit-for-bit"
            else:
                mono = selected.astype(np.float64).mean(axis=1).astype("<f4")
                channel_rule = "float64 arithmetic channel mean then float32 cast"
            verify_candidate_stats(identifier, mono, window)

            native_path = temporary_root / f"{identifier}.native-mono.f32le"
            mono.tofile(native_path)
            resampled_path = temporary_root / f"{identifier}.32000.f32le"
            resample_mono(native_path, resampled_path, media["sample_rate_hz"])
            final_bytes = resampled_path.read_bytes()
            require_equal(f"{identifier} final bytes", len(final_bytes), 384000)
            final = np.frombuffer(final_bytes, dtype="<f4")
            require_equal(f"{identifier} final elements", final.size, 96000)
            if not np.isfinite(final).all():
                raise RuntimeError(f"{identifier}: nonfinite final sample")
            final_2d = np.ascontiguousarray(final.reshape(1, 96000), dtype="<f4")

            raw_destination = args.output_dir / f"{identifier}.f32le"
            raw_temporary = raw_destination.with_suffix(".f32le.tmp")
            raw_temporary.write_bytes(final_2d.tobytes(order="C"))
            os.replace(raw_temporary, raw_destination)
            npy_destination = args.output_dir / f"{identifier}.npy"
            npy_temporary = npy_destination.with_suffix(".npy.tmp")
            with npy_temporary.open("wb") as output:
                np.save(output, final_2d, allow_pickle=False)
            os.replace(npy_temporary, npy_destination)

            stats = waveform_stats(final_2d)
            result_entry = {
                "id": identifier,
                "scientific_name": frozen["scientific_name"],
                "common_name": frozen["common_name"],
                "role": frozen["role"],
                "original_sha256": original_sha256,
                "source_sample_rate_hz": media["sample_rate_hz"],
                "source_channels": media["channels"],
                "source_start_sample_frame": start,
                "source_end_sample_frame_exclusive": end,
                "source_window_sample_frames": window["source_sample_frames"],
                "channel_rule_applied": channel_rule,
                "resampling": (
                    "GStreamer audioresample 1.28.2 Kaiser quality=10, "
                    "sinc-filter-mode=full, sinc-filter-interpolation=none"
                ),
                "amplitude_rule": "preserved; no normalization or signal processing",
                "dtype": "float32 little-endian",
                "shape": [1, 96000],
                **stats,
                "raw_float32_sha256": sha256_bytes(
                    final_2d.tobytes(order="C")
                ),
                "npy_sha256": sha256_file(npy_destination),
                "raw_payload_path": str(raw_destination),
                "npy_path": str(npy_destination),
            }
            result_entries.append(result_entry)
            print(
                f"{identifier}: {result_entry['raw_float32_sha256']} "
                f"min={stats['minimum']:.9g} max={stats['maximum']:.9g} "
                f"rms={stats['rms']:.9g}"
            )

    result = {
        "schema": "birdnet-v3-pre-m5-biological-audio-preparation-result",
        "version": 1,
        "state": "SEVEN_WINDOWS_PREPARED",
        "scope": "H1_ENGINEERING_DEVELOPMENT",
        "recorded_utc": datetime.now(UTC).isoformat(),
        "source_manifest_sha256": EXPECTED_SOURCE_MANIFEST_SHA256,
        "prospective_window_manifest_sha256": EXPECTED_WINDOW_MANIFEST_SHA256,
        "preparation_script_sha256": sha256_file(Path(__file__).resolve()),
        "runtime": runtime,
        "transform_contract": windows["transform_contract"],
        "model_output_consulted_for_source_or_window_selection": False,
        "birdnet_inference_performed": False,
        "entries": result_entries,
    }
    temporary_result = args.result.with_suffix(args.result.suffix + ".tmp")
    temporary_result.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary_result, args.result)
    print(f"Wrote result: {args.result}")
    print("No BirdNET inference was run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
