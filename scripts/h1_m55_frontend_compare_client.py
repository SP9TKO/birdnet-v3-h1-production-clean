#!/usr/bin/env python3
"""Run one bounded complete reference-vs-production M55 frontend comparison."""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from h1_usb_client import (  # noqa: E402
    GET_IDENTITY,
    H1Client,
    PING,
    request_json,
)

RUN_M55_FRONTEND_COMPARE = 21
FIXTURE_IDS = {"synthetic": 1, "wren": 2}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CDC device path; default discovers the H1 CDC port")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--fixture", choices=FIXTURE_IDS, default="synthetic")
    parser.add_argument("--output", type=Path, help="Save compact run and identity JSON")
    args = parser.parse_args()

    with H1Client(args.port, args.timeout) as client:
        ping = request_json(client, PING)
        identity = request_json(client, GET_IDENTITY)
        comparison: dict[str, Any] = client.command(
            RUN_M55_FRONTEND_COMPARE,
            struct.pack("<I", FIXTURE_IDS[args.fixture]),
            timeout=args.timeout,
        ).value

    result = {
        "classification": "CHARACTERIZATION_ONLY",
        "fixture": args.fixture,
        "ping": ping,
        "identity": identity,
        "comparison": comparison,
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    if comparison.get("ok") is not True:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
