#!/usr/bin/env python3
"""Minimal read-only Git bindings for the pinned M4 runtime.

The accepted Python 3.12.14 image intentionally does not carry a Git CLI.
These helpers read loose Git objects and refs directly so qualification guards
can bind themselves to committed state without importing host libraries into
the pinned runtime.  They never update refs, the index, or the worktree.
"""

from __future__ import annotations

import hashlib
import json
import zlib
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


def repository_root(start: Path | None = None) -> Path:
    current = (start or Path.cwd()).resolve()
    for candidate in (current, *current.parents):
        if (candidate / ".git").exists():
            return candidate
    raise RuntimeError("unable to locate repository root")


ROOT = repository_root(Path(__file__).resolve())


def git_dir() -> Path:
    marker = ROOT / ".git"
    if marker.is_dir():
        return marker
    value = marker.read_text().strip()
    if not value.startswith("gitdir: "):
        raise RuntimeError("unsupported .git file")
    target = Path(value.removeprefix("gitdir: "))
    return target if target.is_absolute() else (ROOT / target).resolve()


GIT_DIR = git_dir()


def _read_ref_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    value = path.read_text().strip()
    if value.startswith("ref: "):
        return resolve_ref(value.removeprefix("ref: "))
    return value


def resolve_ref(name: str) -> str:
    if len(name) == 40 and all(character in "0123456789abcdef" for character in name):
        return name
    candidates = [name] if name.startswith("refs/") else [name, f"refs/heads/{name}", f"refs/tags/{name}"]
    for candidate in candidates:
        value = _read_ref_file(GIT_DIR / candidate)
        if value is not None:
            return value
    packed = GIT_DIR / "packed-refs"
    if packed.is_file():
        wanted = set(candidates)
        for line in packed.read_text().splitlines():
            if not line or line[0] in "#^":
                continue
            oid, ref = line.split(" ", 1)
            if ref in wanted:
                return oid
    raise RuntimeError(f"unable to resolve Git ref {name}")


def head_commit() -> str:
    return resolve_ref("HEAD")


def current_branch() -> str:
    value = (GIT_DIR / "HEAD").read_text().strip()
    prefix = "ref: refs/heads/"
    if not value.startswith(prefix):
        raise RuntimeError("detached HEAD is not permitted for this freeze")
    return value.removeprefix(prefix)


def read_object(oid: str) -> tuple[str, bytes]:
    path = GIT_DIR / "objects" / oid[:2] / oid[2:]
    if not path.is_file():
        raise RuntimeError(f"required loose Git object is unavailable: {oid}")
    raw = zlib.decompress(path.read_bytes())
    header, payload = raw.split(b"\0", 1)
    kind, size_text = header.decode("ascii").split(" ", 1)
    if int(size_text) != len(payload):
        raise RuntimeError(f"Git object length mismatch: {oid}")
    actual = hashlib.sha1(raw).hexdigest()
    if actual != oid:
        raise RuntimeError(f"Git object identity mismatch: {oid} != {actual}")
    return kind, payload


def commit_metadata(commit: str) -> dict[str, Any]:
    oid = resolve_ref(commit)
    kind, payload = read_object(oid)
    if kind != "commit":
        raise RuntimeError(f"Git object is not a commit: {oid}")
    header = payload.split(b"\n\n", 1)[0].decode("utf-8", "strict")
    tree = None
    parents = []
    for line in header.splitlines():
        if line.startswith("tree "):
            tree = line.removeprefix("tree ")
        elif line.startswith("parent "):
            parents.append(line.removeprefix("parent "))
    if tree is None:
        raise RuntimeError(f"commit lacks a tree: {oid}")
    return {"oid": oid, "tree": tree, "parents": parents}


def _tree_entries(tree_oid: str) -> dict[str, tuple[str, str]]:
    kind, payload = read_object(tree_oid)
    if kind != "tree":
        raise RuntimeError(f"Git object is not a tree: {tree_oid}")
    entries: dict[str, tuple[str, str]] = {}
    offset = 0
    while offset < len(payload):
        space = payload.index(b" ", offset)
        nul = payload.index(b"\0", space)
        mode = payload[offset:space].decode("ascii")
        name = payload[space + 1 : nul].decode("utf-8", "surrogateescape")
        object_start = nul + 1
        object_end = object_start + 20
        entries[name] = (mode, payload[object_start:object_end].hex())
        offset = object_end
    return entries


def _entry_at(commit: str, path: str | Path) -> tuple[str, str]:
    parts = PurePosixPath(Path(path).as_posix()).parts
    tree_oid = commit_metadata(commit)["tree"]
    entry: tuple[str, str] | None = None
    for index, part in enumerate(parts):
        entry = _tree_entries(tree_oid).get(part)
        if entry is None:
            raise FileNotFoundError(f"{path} is absent from commit {resolve_ref(commit)}")
        mode, oid = entry
        if index != len(parts) - 1:
            if mode != "40000":
                raise FileNotFoundError(f"non-tree component in committed path {path}")
            tree_oid = oid
    if entry is None:
        raise FileNotFoundError(f"empty committed path: {path}")
    return entry


def read_commit_file(commit: str, path: str | Path) -> bytes:
    mode, oid = _entry_at(commit, path)
    if mode == "40000":
        raise IsADirectoryError(str(path))
    kind, payload = read_object(oid)
    if kind != "blob":
        raise RuntimeError(f"committed path is not a blob: {path}")
    return payload


def list_commit_files(commit: str, prefix: str | Path = ".") -> dict[str, bytes]:
    normalized = PurePosixPath(Path(prefix).as_posix())
    base_parts = () if str(normalized) in (".", "") else normalized.parts
    tree_oid = commit_metadata(commit)["tree"]
    for part in base_parts:
        mode, tree_oid = _tree_entries(tree_oid).get(part, ("", ""))
        if mode != "40000":
            raise FileNotFoundError(f"committed directory is absent: {prefix}")
    output: dict[str, bytes] = {}

    def walk(oid: str, parent: PurePosixPath) -> None:
        for name, (mode, child_oid) in sorted(_tree_entries(oid).items()):
            child = parent / name
            if mode == "40000":
                walk(child_oid, child)
            else:
                kind, payload = read_object(child_oid)
                if kind != "blob":
                    raise RuntimeError(f"non-blob tree leaf: {child}")
                output[child.as_posix()] = payload

    walk(tree_oid, PurePosixPath(*base_parts))
    return output


def is_ancestor(ancestor: str, descendant: str) -> bool:
    wanted = resolve_ref(ancestor)
    pending = [resolve_ref(descendant)]
    visited: set[str] = set()
    while pending:
        current = pending.pop()
        if current == wanted:
            return True
        if current in visited:
            continue
        visited.add(current)
        pending.extend(commit_metadata(current)["parents"])
    return False


def committed_file_identity(commit: str, path: str | Path) -> dict[str, Any]:
    payload = read_commit_file(commit, path)
    return {
        "path": Path(path).as_posix(),
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def worktree_file_identity(path: str | Path) -> dict[str, Any]:
    normalized = Path(path)
    payload = (ROOT / normalized).read_bytes()
    return {
        "path": normalized.as_posix(),
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def require_paths_match_commit(commit: str, paths: Iterable[str | Path]) -> None:
    failures = []
    for path in paths:
        committed = committed_file_identity(commit, path)
        observed = worktree_file_identity(path)
        if committed != observed:
            failures.append({"committed": committed, "worktree": observed})
    if failures:
        raise RuntimeError(f"worktree differs from committed freeze files: {failures}")


def require_implementation_freeze(commit: str, record_path: str | Path) -> dict[str, Any]:
    record_payload = read_commit_file(commit, record_path)
    record = json.loads(record_payload)
    failures = []
    for item in record["files"]:
        committed = committed_file_identity(commit, item["path"])
        observed = worktree_file_identity(item["path"])
        expected = {key: item[key] for key in ("path", "bytes", "sha256")}
        if committed != expected or observed != expected:
            failures.append(
                {"expected": expected, "committed": committed, "worktree": observed}
            )
    if failures:
        raise RuntimeError(f"implementation freeze mismatch: {failures}")
    return record
