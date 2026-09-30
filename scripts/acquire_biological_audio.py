#!/usr/bin/env python3
"""Acquire the frozen Pre-M5 biological-audio originals into ignored scratch.

This tool downloads only the sources named in the committed manifest, verifies
their exact byte identities, and writes a local acquisition lock. It does not
decode audio, select windows, or run BirdNET.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = (
    REPO_ROOT / "records" / "development" / "BIOLOGICAL_AUDIO_SOURCES.json"
)
DEFAULT_OUTPUT_DIR = (
    REPO_ROOT / ".development-work" / "biological-audio" / "originals"
)
DEFAULT_LOCK = (
    REPO_ROOT
    / ".development-work"
    / "biological-audio"
    / "BIOLOGICAL_AUDIO_LOCK.json"
)
USER_AGENT = (
    "BirdNET-V3-PreM5-AudioAcquisition/2.0 "
    "(development reproducibility research)"
)
RETRYABLE_HTTP_STATUS = {408, 425, 429, 500, 502, 503, 504}


def file_hashes(path: Path) -> tuple[str, str, int]:
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            size += len(block)
            sha1.update(block)
            sha256.update(block)
    return sha1.hexdigest(), sha256.hexdigest(), size


def sha256_file(path: Path) -> str:
    return file_hashes(path)[1]


def validate_original(path: Path, entry: dict[str, object]) -> dict[str, object]:
    observed_sha1, observed_sha256, observed_size = file_hashes(path)
    expected_size = entry.get("expected_size_bytes")
    expected_sha1 = entry.get("expected_sha1")
    expected_sha256 = entry.get("expected_sha256")
    if expected_size is not None and observed_size != expected_size:
        raise RuntimeError(
            f"{entry['id']}: size mismatch: got {observed_size}, "
            f"expected {expected_size}"
        )
    if expected_sha1 is not None and observed_sha1.lower() != str(expected_sha1).lower():
        raise RuntimeError(
            f"{entry['id']}: SHA-1 mismatch: got {observed_sha1}, "
            f"expected {expected_sha1}"
        )
    if (
        expected_sha256 is not None
        and observed_sha256.lower() != str(expected_sha256).lower()
    ):
        raise RuntimeError(
            f"{entry['id']}: SHA-256 mismatch: got {observed_sha256}, "
            f"expected {expected_sha256}"
        )
    with path.open("rb") as source:
        magic = source.read(4)
    if magic != b"OggS":
        raise RuntimeError(f"{entry['id']}: unexpected container magic {magic!r}")
    return {
        "observed_size_bytes": observed_size,
        "observed_sha1": observed_sha1,
        "observed_sha256": observed_sha256,
        "source_expected_size_verified": expected_size is not None,
        "source_expected_sha1_verified": expected_sha1 is not None,
        "committed_sha256_verified": expected_sha256 is not None,
    }


def retry_delay_seconds(
    error: BaseException,
    attempt: int,
    initial_backoff: float,
    maximum_backoff: float,
) -> float:
    if isinstance(error, urllib.error.HTTPError):
        retry_after = error.headers.get("Retry-After")
        if retry_after is not None:
            try:
                return min(maximum_backoff, max(0.0, float(retry_after)))
            except ValueError:
                pass
    return min(maximum_backoff, initial_backoff * (2 ** (attempt - 1)))


def download_with_retry(
    url: str,
    destination: Path,
    max_attempts: int,
    initial_backoff: float,
    maximum_backoff: float,
) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    for attempt in range(1, max_attempts + 1):
        try:
            with (
                urllib.request.urlopen(request, timeout=120) as response,
                destination.open("wb") as output,
            ):
                shutil.copyfileobj(response, output, length=1024 * 1024)
                return response.geturl()
        except urllib.error.HTTPError as error:
            if error.code not in RETRYABLE_HTTP_STATUS or attempt == max_attempts:
                raise
            delay = retry_delay_seconds(
                error, attempt, initial_backoff, maximum_backoff
            )
        except urllib.error.URLError as error:
            if attempt == max_attempts:
                raise
            delay = retry_delay_seconds(
                error, attempt, initial_backoff, maximum_backoff
            )
        destination.unlink(missing_ok=True)
        print(
            f"  retry: attempt {attempt + 1}/{max_attempts} after "
            f"{delay:.1f}s",
            file=sys.stderr,
        )
        time.sleep(delay)
    raise AssertionError("unreachable")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        help="Acquire only a listed manifest id; repeatable.",
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--inter-download-delay", type=float, default=3.0)
    parser.add_argument("--max-attempts", type=int, default=6)
    parser.add_argument("--initial-backoff", type=float, default=10.0)
    parser.add_argument("--maximum-backoff", type=float, default=60.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    entries = manifest["entries"]
    wanted = set(args.only)
    selected = [entry for entry in entries if not wanted or entry["id"] in wanted]
    missing = wanted - {entry["id"] for entry in selected}
    if missing:
        raise SystemExit(f"Unknown --only ids: {sorted(missing)}")
    if args.max_attempts < 1:
        raise SystemExit("--max-attempts must be positive")

    if args.dry_run:
        for entry in selected:
            print(f"{entry['id']}: {entry['download_url']}")
        return 0

    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    lock_entries: list[dict[str, object]] = []
    completed_downloads = 0

    for entry in selected:
        destination = args.output_dir / entry["commons_filename"]
        print(f"{entry['id']}: {entry['scientific_name']}")
        print(f"  source: {entry['source_page']}")
        print(f"  file:   {destination}")
        final_url: str | None = None

        if args.force or not destination.exists():
            if completed_downloads:
                time.sleep(max(0.0, args.inter_download_delay))
            file_descriptor, temporary_name = tempfile.mkstemp(
                prefix=destination.name + ".",
                suffix=".part",
                dir=str(args.output_dir),
            )
            os.close(file_descriptor)
            temporary = Path(temporary_name)
            try:
                final_url = download_with_retry(
                    entry["download_url"],
                    temporary,
                    args.max_attempts,
                    args.initial_backoff,
                    args.maximum_backoff,
                )
                observed = validate_original(temporary, entry)
                os.replace(temporary, destination)
                completed_downloads += 1
            finally:
                temporary.unlink(missing_ok=True)
        else:
            observed = validate_original(destination, entry)

        lock_entry = {
            "id": entry["id"],
            "scientific_name": entry["scientific_name"],
            "common_name": entry["common_name"],
            "role": entry["role"],
            "commons_filename": entry["commons_filename"],
            "source_page": entry["source_page"],
            "download_url": entry["download_url"],
            "resolved_download_url": final_url,
            "author": entry["author"],
            "license": entry["license"],
            "license_url": entry["license_url"],
            "location": entry.get("location"),
            "recording_date": entry.get("recording_date"),
            **observed,
            "local_path": str(destination),
        }
        lock_entries.append(lock_entry)
        print(f"  bytes:  {observed['observed_size_bytes']}")
        print(f"  sha1:   {observed['observed_sha1']}")
        print(f"  sha256: {observed['observed_sha256']}")

    lock = {
        "schema": "birdnet-v3-pre-m5-biological-audio-local-lock",
        "version": 2,
        "state": "COMPLETE" if len(selected) == len(entries) else "PARTIAL",
        "source_manifest": str(args.manifest),
        "source_manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
        "source_manifest_state": manifest["state"],
        "acquisition_script_sha256": sha256_file(Path(__file__).resolve()),
        "python": ".".join(str(part) for part in sys.version_info[:3]),
        "acquired_unix_time": int(time.time()),
        "window_selection_performed": False,
        "birdnet_inference_performed": False,
        "entries": lock_entries,
    }
    temporary_lock = args.lock.with_suffix(args.lock.suffix + ".tmp")
    temporary_lock.write_text(
        json.dumps(lock, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary_lock, args.lock)
    print(f"\nWrote lock: {args.lock}")
    print("No windows were selected and no BirdNET inference was run.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
