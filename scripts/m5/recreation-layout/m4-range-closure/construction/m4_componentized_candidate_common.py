#!/usr/bin/env python3
"""Shared helpers for the prospectively frozen componentized M4 candidate."""

from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from h1_model import (
    array_sha256,
    file_identity,
    tree_identity,
    write_json,
)
from m4_componentized_common import (
    BANNED_DEPENDENCIES,
    boundary_manifest,
    fixture_corpora,
    frontend_value,
    load_json,
    run_stage_slice,
    slice_stages,
    source_binding,
    verify_criteria,
)


CANDIDATE_CRITERIA_SCHEMA = "birdnet-clean-m4-componentized-candidate-criteria"
CANDIDATE_CRITERIA_R2_SCHEMA = (
    "birdnet-clean-m4-componentized-candidate-audit-correction-criteria"
)
COMPONENT_NAMES = ("frontend", "backbone", "gem", "classifier")
A16W8_COMPONENTS = ("backbone", "classifier")
FP32_COMPONENTS = ("frontend", "gem")


def candidate_criteria(path: Path) -> dict[str, Any]:
    raw = load_json(path)
    if raw.get("schema") == CANDIDATE_CRITERIA_SCHEMA:
        criteria = verify_criteria(path, CANDIDATE_CRITERIA_SCHEMA)
    elif raw.get("schema") == CANDIDATE_CRITERIA_R2_SCHEMA:
        overlay = verify_criteria(path, CANDIDATE_CRITERIA_R2_SCHEMA)
        base_path = Path(overlay["base_criteria"]["path"])
        base_identity = file_identity(base_path)
        if base_identity["sha256"] != overlay["base_criteria"]["sha256"]:
            raise RuntimeError(
                f"base criteria mismatch for {base_path}: "
                f"{base_identity['sha256']} != {overlay['base_criteria']['sha256']}"
            )
        base = load_json(base_path)
        if (
            base.get("schema") != CANDIDATE_CRITERIA_SCHEMA
            or base.get("state") != "VALIDATED"
            or base.get("frozen_before_results") is not True
        ):
            raise RuntimeError("base componentized criteria are not a valid frozen contract")
        for record in overlay["failed_attempt_evidence"].values():
            evidence_path = Path(record["path"])
            evidence_identity = file_identity(evidence_path)
            if evidence_identity["sha256"] != record["sha256"]:
                raise RuntimeError(
                    f"failed-attempt evidence mismatch for {evidence_path}: "
                    f"{evidence_identity['sha256']} != {record['sha256']}"
                )
        expected_artifact_names = {
            "frontend",
            "backbone_a",
            "backbone_b",
            "gem",
            "classifier_a",
            "classifier_b",
        }
        if set(overlay["candidate_artifacts"]) != expected_artifact_names:
            raise RuntimeError("audit correction overlay has an incomplete artifact binding")
        invariants = overlay["contract_invariants"]
        if not all(
            invariants.get(name) is True
            for name in (
                "candidate_artifacts_unchanged",
                "candidate_sources_unchanged",
                "corpora_unchanged",
                "numerical_limits_unchanged",
                "range_requirements_unchanged",
                "repeatability_requirements_unchanged",
                "structural_contract_unchanged",
            )
        ):
            raise RuntimeError("audit correction overlay does not preserve the frozen contract")
        criteria = dict(base)
        criteria["implementation_sha256"] = overlay["implementation_sha256"]
        criteria["audit_correction_overlay"] = {
            "path": str(path),
            "identity": file_identity(path),
            "base_criteria": overlay["base_criteria"],
            "failed_attempt_evidence": overlay["failed_attempt_evidence"],
            "candidate_artifacts": overlay["candidate_artifacts"],
            "harness_corrections": overlay["harness_corrections"],
            "contract_invariants": invariants,
        }
    else:
        raise RuntimeError(f"unexpected criteria schema in {path}: {raw.get('schema')}")
    verify_frozen_evidence(criteria)
    return criteria


def verify_frozen_evidence(criteria: dict[str, Any]) -> None:
    for group in ("pre_candidate_evidence_sha256", "blocked_predecessor_evidence_sha256"):
        for name, expected in criteria[group].items():
            path = Path(name)
            actual = file_identity(path)["sha256"]
            if actual != expected:
                raise RuntimeError(
                    f"frozen evidence mismatch for {path}: {actual} != {expected}"
                )
    proof = load_json(Path(criteria["pre_candidate_proof"]["result_path"]))
    if (
        proof.get("disposition") != "VALIDATED"
        or proof.get("a16w8_conversion_started") is not False
        or proof.get("component_candidate_produced") is not False
    ):
        raise RuntimeError("pre-candidate component proof is not a clean validated freeze")
    predecessor = load_json(Path(criteria["blocked_predecessor"]["state_path"]))
    if predecessor.get("disposition") != "BLOCKED":
        raise RuntimeError("the previous corrective M4 result is no longer BLOCKED")


def dependency_check(paths: Iterable[Path]) -> dict[str, Any]:
    roots: set[str] = set()
    checked = []
    for path in paths:
        tree = ast.parse(path.read_text())
        checked.append(str(path))
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                roots.update(alias.name.split(".")[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                roots.add(node.module.split(".")[0])
    found = sorted(roots & BANNED_DEPENDENCIES)
    if found:
        raise RuntimeError(f"forbidden implementation dependencies: {found}")
    return {
        "state": "VALIDATED",
        "checked": checked,
        "forbidden": sorted(BANNED_DEPENDENCIES),
        "found": found,
    }


def verify_component_tree(
    component: str, component_saved_model: Path, criteria: dict[str, Any]
) -> dict[str, Any]:
    if component not in COMPONENT_NAMES:
        raise RuntimeError(f"unknown component: {component}")
    identity = tree_identity(component_saved_model)
    expected = criteria["component_sources"][component]["tree_sha256"]
    if identity["tree_sha256"] != expected:
        raise RuntimeError(
            f"{component} SavedModel tree mismatch: "
            f"{identity['tree_sha256']} != {expected}"
        )
    return identity


def _write_array_bank(root: Path, name: str, rows: list[np.ndarray]) -> None:
    destination = root / name
    destination.mkdir(parents=True, exist_ok=False)
    for index, row in enumerate(rows):
        np.save(destination / f"{index:04d}.npy", np.asarray(row), allow_pickle=False)


def prepare_calibration_bank(
    *,
    saved_model: Path,
    labels: Path,
    criteria: dict[str, Any],
    root: Path,
    manifest_path: Path,
    tf: Any,
) -> dict[str, Any]:
    if root.exists():
        raise RuntimeError(f"refusing to reuse calibration bank: {root}")
    if manifest_path.exists():
        raise RuntimeError(f"refusing to overwrite calibration manifest: {manifest_path}")
    source, payload = source_binding(saved_model, labels)
    calibration, _, corpus = fixture_corpora(criteria)
    module, report = slice_stages(payload, ("shared_feature", "embedding"))
    expected_slice = criteria["m3_slices"]["calibration_reference"]
    if report["artifact"] != expected_slice:
        raise RuntimeError(
            f"calibration StableHLO slice changed: {report['artifact']} != {expected_slice}"
        )
    loaded = tf.saved_model.load(str(saved_model))
    boundaries: dict[str, list[np.ndarray]] = {
        "frontend_backbone_input": [],
        "shared_feature": [],
        "gem_embedding": [],
        "classifier_input": [],
    }
    for waveform in calibration:
        frontend = frontend_value(loaded, waveform, tf)
        reference = run_stage_slice(loaded, module, report, frontend, tf)
        boundaries["frontend_backbone_input"].append(
            np.asarray(frontend.numpy(), dtype=np.float32)
        )
        boundaries["shared_feature"].append(
            np.asarray(reference["shared_feature"], dtype=np.float32)
        )
        boundaries["gem_embedding"].append(
            np.asarray(reference["embedding"], dtype=np.float32)
        )
        boundaries["classifier_input"].append(
            np.asarray(reference["embedding"], dtype=np.float32)
        )

    manifests = {
        name: boundary_manifest(name, rows) for name, rows in boundaries.items()
    }
    for name, expected in criteria["calibration"]["boundary_aggregate_sha256"].items():
        actual = manifests[name]["aggregate_sha256"]
        if actual != expected:
            raise RuntimeError(
                f"clean calibration boundary {name} changed: {actual} != {expected}"
            )
    root.mkdir(parents=True, exist_ok=False)
    _write_array_bank(root, "frontend_backbone_input", boundaries["frontend_backbone_input"])
    _write_array_bank(root, "classifier_input", boundaries["classifier_input"])
    record = {
        "schema": "birdnet-clean-m4-componentized-calibration-bank",
        "version": 1,
        "state": "VALIDATED",
        "source_binding": source,
        "corpus": corpus,
        "m3_calibration_slice": report,
        "boundary_manifests": manifests,
        "persisted_boundaries": ["frontend_backbone_input", "classifier_input"],
        "bank_root": str(root),
        "historical_calibration_payload_used": False,
        "previous_project_payload_used": False,
    }
    write_json(manifest_path, record)
    return record


def load_calibration_bank(
    component: str,
    root: Path,
    manifest_path: Path,
    criteria: dict[str, Any],
) -> tuple[list[np.ndarray], dict[str, Any]]:
    boundary_by_component = {
        "backbone": "frontend_backbone_input",
        "classifier": "classifier_input",
    }
    if component not in boundary_by_component:
        raise RuntimeError(f"component {component} does not consume a calibration bank")
    record = load_json(manifest_path)
    if (
        record.get("schema") != "birdnet-clean-m4-componentized-calibration-bank"
        or record.get("state") != "VALIDATED"
        or record.get("historical_calibration_payload_used") is not False
        or record.get("previous_project_payload_used") is not False
    ):
        raise RuntimeError("calibration bank provenance is not valid")
    expected_source = criteria["m3_authority"]["saved_model_tree_sha256"]
    if (
        record["source_binding"]["canonical_m3_saved_model"]["tree_sha256"]
        != expected_source
    ):
        raise RuntimeError("calibration bank source is not canonical M3")
    name = boundary_by_component[component]
    expected_aggregate = criteria["calibration"]["boundary_aggregate_sha256"][name]
    if record["boundary_manifests"][name]["aggregate_sha256"] != expected_aggregate:
        raise RuntimeError(f"calibration manifest identity changed for {name}")
    directory = root / name
    paths = sorted(directory.glob("*.npy"))
    expected_count = criteria["calibration"]["count"]
    if len(paths) != expected_count:
        raise RuntimeError(f"calibration bank {name} has {len(paths)} rows")
    rows = [np.load(path, allow_pickle=False) for path in paths]
    observed = boundary_manifest(name, rows)
    if observed["aggregate_sha256"] != expected_aggregate:
        raise RuntimeError(f"calibration bank payload identity changed for {name}")
    expected_rows = record["boundary_manifests"][name]["rows"]
    if [array_sha256(row) for row in rows] != [entry["sha256"] for entry in expected_rows]:
        raise RuntimeError(f"calibration bank row identity changed for {name}")
    return rows, observed


def convert_component(
    component: str,
    component_saved_model: Path,
    calibration_rows: list[np.ndarray] | None,
    tf: Any,
) -> bytes:
    converter = tf.lite.TFLiteConverter.from_saved_model(str(component_saved_model))
    converter.allow_custom_ops = False
    if component in FP32_COMPONENTS:
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]
    elif component in A16W8_COMPONENTS:
        if not calibration_rows:
            raise RuntimeError(f"{component} calibration rows are missing")
        converter.optimizations = [tf.lite.Optimize.DEFAULT]

        def representative_dataset() -> Any:
            for value in calibration_rows:
                yield [np.asarray(value, dtype=np.float32)]

        converter.representative_dataset = representative_dataset
        converter.target_spec.supported_ops = [
            tf.lite.OpsSet.EXPERIMENTAL_TFLITE_BUILTINS_ACTIVATIONS_INT16_WEIGHTS_INT8
        ]
        converter.inference_input_type = tf.int16
        converter.inference_output_type = tf.int16
    else:
        raise RuntimeError(f"unsupported component: {component}")
    return bytes(converter.convert())


def shape(detail: dict[str, Any]) -> list[int]:
    return [int(value) for value in np.asarray(detail["shape"]).tolist()]


def dtype(detail: dict[str, Any]) -> str:
    return np.dtype(detail["dtype"]).name


def quantization(detail: dict[str, Any]) -> tuple[np.ndarray, np.ndarray, int]:
    parameters = detail.get("quantization_parameters", {})
    scales = np.asarray(parameters.get("scales", []), dtype=np.float64)
    zero_points = np.asarray(parameters.get("zero_points", []), dtype=np.int64)
    dimension = int(parameters.get("quantized_dimension", 0))
    return scales, zero_points, dimension


def quantize_array(value: Any, detail: dict[str, Any]) -> np.ndarray:
    scales, zero_points, _ = quantization(detail)
    if scales.size != 1 or zero_points.size != 1:
        raise RuntimeError(f"tensor {detail['index']} is not per-tensor quantized")
    target = np.dtype(detail["dtype"])
    limits = np.iinfo(target)
    quantized = np.rint(np.asarray(value, dtype=np.float64) / scales[0] + zero_points[0])
    return np.clip(quantized, limits.min, limits.max).astype(target)


def dequantize_array(value: Any, detail: dict[str, Any]) -> np.ndarray:
    scales, zero_points, _ = quantization(detail)
    if scales.size != 1 or zero_points.size != 1:
        raise RuntimeError(f"tensor {detail['index']} is not per-tensor quantized")
    return (
        np.asarray(value, dtype=np.float32) - np.float32(zero_points[0])
    ) * np.float32(scales[0])


def interpreter(path: Path, tf: Any, preserve_all: bool = True) -> Any:
    resolver = tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES
    instance = tf.lite.Interpreter(
        model_path=str(path),
        num_threads=1,
        experimental_op_resolver_type=resolver,
        experimental_preserve_all_tensors=preserve_all,
    )
    instance.allocate_tensors()
    return instance


def graph(interpreter_instance: Any) -> tuple[dict[int, dict[str, Any]], list[dict[str, Any]], dict[int, int], dict[int, list[int]]]:
    details = {
        int(detail["index"]): detail
        for detail in interpreter_instance.get_tensor_details()
    }
    operations = list(interpreter_instance._get_ops_details())
    producers: dict[int, int] = {}
    consumers: dict[int, list[int]] = {}
    for operation_index, operation in enumerate(operations):
        for tensor in operation.get("outputs", []):
            tensor_index = int(tensor)
            if tensor_index >= 0:
                producers[tensor_index] = operation_index
        for tensor in operation.get("inputs", []):
            tensor_index = int(tensor)
            if tensor_index >= 0:
                consumers.setdefault(tensor_index, []).append(operation_index)
    return details, operations, producers, consumers


def tensor_record(detail: dict[str, Any]) -> dict[str, Any]:
    scales, zero_points, dimension = quantization(detail)
    return {
        "index": int(detail["index"]),
        "name": str(detail["name"]),
        "shape": shape(detail),
        "dtype": dtype(detail),
        "quantization": {
            "scale_count": int(scales.size),
            "zero_point_count": int(zero_points.size),
            "quantized_dimension": dimension,
            "scale": float(scales[0]) if scales.size == 1 else None,
            "zero_point": int(zero_points[0]) if zero_points.size == 1 else None,
            "all_zero_points_zero": bool(zero_points.size and np.all(zero_points == 0)),
        },
    }


def operation_inventory(operations: Iterable[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for operation in operations:
        name = str(operation["op_name"])
        counts[name] = counts.get(name, 0) + 1
    return dict(sorted(counts.items()))


def artifact_bundle_identity(entries: dict[str, dict[str, Any]]) -> dict[str, Any]:
    digest = hashlib.sha256()
    rows = []
    for role in ("frontend", "backbone", "gem", "classifier"):
        item = entries[role]
        row = f"{role} {item['sha256']} {item['bytes']}\n"
        digest.update(row.encode())
        rows.append(row.rstrip())
    return {
        "algorithm": "sha256 of ordered '<role> <sha256> <bytes>\\n' rows",
        "rows": rows,
        "sha256": digest.hexdigest(),
    }
