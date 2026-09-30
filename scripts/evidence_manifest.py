#!/usr/bin/env python3
"""Create a compact deterministic manifest for retained gate evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {
        "path": path.as_posix(),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--implementation", type=Path, action="append", default=[])
    args = parser.parse_args()

    output = args.output.resolve()
    files = [
        identity(path)
        for path in sorted(args.root.rglob("*"))
        if path.is_file() and path.resolve() != output
    ]
    implementations = [identity(path) for path in args.implementation]
    result = {
        "schema": "birdnet-clean-evidence-manifest",
        "version": 1,
        "state": "VALIDATED",
        "root": args.root.as_posix(),
        "file_count": len(files),
        "total_bytes": sum(row["bytes"] for row in files),
        "files": files,
        "implementation": implementations,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"manifested {len(files)} evidence files ({result['total_bytes']} bytes)")


if __name__ == "__main__":
    main()
