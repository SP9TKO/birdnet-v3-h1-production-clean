#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
# SPDX-License-Identifier: Apache-2.0
"""Integrity-checked reconstruction of baseline diagnostic H1DP pages."""
from __future__ import annotations

import hashlib
import struct
import zlib

PAGE_HEADER = struct.Struct("<4sHH8I")
PAGE_DATA_BYTES = 6940
MAX_WIRE_BYTES = 7000
H1CP_HEADER_BYTES = 20
MAX_LOGICAL_BYTES = 16383


class DiagnosticPages:
    """Retain exact page bytes and reject inconsistent or incomplete exports."""

    def __init__(self, response_id: int) -> None:
        self.response_id = response_id
        self.pages: dict[int, bytes] = {}
        self.records: list[dict[str, int | str]] = []
        self.identity: tuple[int, int, int] | None = None

    def add(self, payload: bytes) -> None:
        if len(payload) < PAGE_HEADER.size or len(payload) + H1CP_HEADER_BYTES > MAX_WIRE_BYTES:
            raise ValueError("Diagnostic page size violates framing limit")
        magic, version, header_bytes, response_id, index, count, offset, length, total, page_crc, whole_crc = PAGE_HEADER.unpack_from(payload)
        if (magic, version, header_bytes) != (b"H1DP", 1, PAGE_HEADER.size):
            raise ValueError("Invalid diagnostic page schema")
        if response_id != self.response_id:
            raise ValueError("Diagnostic response identity changed")
        if not 0 < total <= MAX_LOGICAL_BYTES or count != (total + PAGE_DATA_BYTES - 1) // PAGE_DATA_BYTES:
            raise ValueError("Invalid diagnostic total length/page count")
        identity = (count, total, whole_crc)
        if self.identity is not None and identity != self.identity:
            raise ValueError("Diagnostic page response metadata changed")
        self.identity = identity
        if index >= count or index in self.pages:
            raise ValueError("Invalid or duplicate diagnostic page index")
        expected_offset = index * PAGE_DATA_BYTES
        if offset != expected_offset or length != min(PAGE_DATA_BYTES, total - expected_offset):
            raise ValueError("Diagnostic page offset/length mismatch")
        data = payload[header_bytes:]
        if len(data) != length:
            raise ValueError("Truncated diagnostic page payload")
        if zlib.crc32(data) & 0xFFFFFFFF != page_crc:
            raise ValueError("Diagnostic page payload CRC mismatch")
        self.pages[index] = data
        self.records.append({"page_index": index, "page_count": count,
                             "payload_offset": offset, "payload_length": length,
                             "total_logical_length": total, "page_crc32": f"{page_crc:08x}",
                             "whole_logical_crc32": f"{whole_crc:08x}",
                             "H1CP_payload_bytes": len(payload),
                             "encoded_wire_bytes": H1CP_HEADER_BYTES + len(payload)})

    @property
    def complete(self) -> bool:
        return self.identity is not None and len(self.pages) == self.identity[0]

    def finish(self) -> bytes:
        if self.identity is None:
            raise ValueError("No diagnostic pages received")
        count, total, whole_crc = self.identity
        if sorted(self.pages) != list(range(count)):
            raise ValueError("Missing diagnostic pages")
        data = b"".join(self.pages[i] for i in range(count))
        if len(data) != total:
            raise ValueError("Reconstructed diagnostic response length mismatch")
        if zlib.crc32(data) & 0xFFFFFFFF != whole_crc:
            raise ValueError("Reconstructed diagnostic whole-response CRC mismatch")
        return data

    def evidence(self) -> dict:
        data = self.finish()
        return {"response_id": self.response_id, "page_count": len(self.pages),
                "pages": self.records, "reconstructed_bytes": len(data),
                "largest_encoded_wire_bytes": max(r["encoded_wire_bytes"] for r in self.records),
                "whole_logical_crc32": f"{zlib.crc32(data) & 0xFFFFFFFF:08x}",
                "reconstructed_sha256": hashlib.sha256(data).hexdigest(),
                "page_indices_contiguous": True, "duplicate_pages": 0,
                "missing_pages": 0, "page_integrity_failures": 0,
                "whole_device_identity_match": True}
