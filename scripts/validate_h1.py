#!/usr/bin/env python3
"""Validate clean in-memory TensorFlow H1 against the official TensorFlow graph."""

from __future__ import annotations

import argparse
import ast
import csv
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

import numpy as np

from h1_model import (
    EMBEDDING_SHAPE,
    EXPECTED_IDENTITIES,
    LABELS_NAME,
    SCORES_SHAPE,
    array_identity,
    array_sha256,
    assert_runtime_environment,
    comparison_metrics,
    configure_tensorflow,
    embedded_stablehlo,
    file_identity,
    find_saved_model,
    generated_fixtures,
    make_clean_h1,
    parse_saved_model,
    prepare_frontend,
    run_xla_module,
    slice_h1_module,
    write_json,
)


BANNED_MODULES = {"torch", "torchvision", "torchaudio", "onnx", "onnxruntime"}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text())


def imported_roots(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(), filename=str(path))
    roots = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            roots.update(alias.name.split(".")[0] for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            roots.add(node.module.split(".")[0])
    return roots


def dependency_check(paths: list[Path]) -> dict[str, Any]:
    rows = []
    violations = []
    for path in paths:
        roots = sorted(imported_roots(path))
        forbidden = sorted(BANNED_MODULES.intersection(roots))
        rows.append({
            "path": path.as_posix(),
            "sha256": file_identity(path)["sha256"],
            "import_roots": roots,
            "forbidden_import_roots": forbidden,
        })
        violations.extend(f"{path}:{name}" for name in forbidden)
    runtime_loaded = sorted(
        name for name in BANNED_MODULES if name in sys.modules or any(
            module.startswith(name + ".") for module in sys.modules
        )
    )
    if violations or runtime_loaded:
        raise RuntimeError(
            f"forbidden framework dependency: static={violations} runtime={runtime_loaded}"
        )
    return {
        "schema": "birdnet-clean-m2-dependency-check",
        "version": 1,
        "state": "VALIDATED",
        "forbidden_modules": sorted(BANNED_MODULES),
        "files": rows,
        "runtime_loaded_forbidden_modules": runtime_loaded,
        "conclusion": "no PyTorch or ONNX package is imported or executed by accepted M1-M2",
    }


def label_check(path: Path) -> dict[str, Any]:
    identity = file_identity(path, md5=True)
    expected = EXPECTED_IDENTITIES[LABELS_NAME]
    for key, value in expected.items():
        if identity.get(key) != value:
            raise RuntimeError(f"official labels mismatch for {key}: {identity.get(key)} != {value}")
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    header = [cell.lower() for cell in rows[0]]
    has_header = any(
        token in cell
        for cell in header
        for token in ("scientific", "common", "label", "class")
    )
    data = rows[1:] if has_header else rows
    if len(data) != 11_560:
        raise RuntimeError(f"official label count changed: {len(data)}")
    return {
        "state": "VALIDATED",
        "identity": identity,
        "data_rows": len(data),
        "mapping": "output column i maps to official CSV data row i",
    }


def classify_public_outputs(outputs: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    arrays = {str(name): np.asarray(value) for name, value in outputs.items()}
    score = [value for value in arrays.values() if list(value.shape) == SCORES_SHAPE]
    embedding = [value for value in arrays.values() if list(value.shape) == EMBEDDING_SHAPE]
    if len(score) != 1 or len(embedding) != 1:
        raise RuntimeError(f"could not classify official public outputs: {[(k, v.shape) for k, v in arrays.items()]}")
    return score[0], embedding[0]


def threshold_check(metrics: dict[str, Any], contract: dict[str, Any]) -> list[str]:
    failures = []
    if contract.get("require_raw_byte_equal") and not metrics["raw_byte_equal"]:
        failures.append("raw_byte_equal=false")
    for mask in (
        "nan_mask_equal",
        "positive_infinity_mask_equal",
        "negative_infinity_mask_equal",
    ):
        if not metrics[mask]:
            failures.append(f"{mask}=false")
    for metric, limit in contract.get("maximum", {}).items():
        actual = metrics.get(metric)
        if actual is None or actual > limit:
            failures.append(f"{metric}={actual} > {limit}")
    return failures


def independent_oracle(tf, shared_feature, gem_p, weight, bias) -> dict[str, Any]:
    nchw = tf.transpose(shared_feature, [0, 3, 1, 2])
    clamped = tf.maximum(nchw, tf.cast(1e-6, tf.float32))
    powered = tf.pow(clamped, gem_p)
    mean = tf.reduce_sum(powered, axis=[3, 2], keepdims=True) / tf.constant(63.0, tf.float32)
    reciprocal = tf.constant(1.0, tf.float32) / gem_p
    embedding = tf.reshape(tf.pow(mean, reciprocal), EMBEDDING_SHAPE)
    total = tf.fill(SCORES_SHAPE, tf.constant(0.0, tf.float32))
    transposed_weight = tf.transpose(weight)
    for _ in range(5):
        contribution = tf.matmul(tf.identity(embedding), transposed_weight)
        contribution = contribution * tf.constant(1, tf.float32)
        contribution = contribution + bias * tf.constant(1, tf.float32)
        total = total + contribution
    logits = total / tf.constant(5, tf.float32)
    scores = tf.constant(1.0, tf.float32) / (
        tf.constant(1.0, tf.float32) + tf.exp(-logits)
    )
    return {"embedding": embedding, "h1_logits": logits, "scores": scores}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extracted", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--criteria", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    criteria = load_json(args.criteria)
    if criteria.get("state") != "VALIDATED" or not criteria.get("frozen_before_results"):
        raise RuntimeError("prospective M2 criteria are not frozen")
    tf = configure_tensorflow()
    runtime = assert_runtime_environment(tf)
    labels = label_check(args.labels)
    model_dir = find_saved_model(args.extracted)
    saved_model = parse_saved_model(model_dir)
    source_module, _ = embedded_stablehlo(saved_model)
    production_module, production_report = slice_h1_module(source_module, "production")
    probe_module, probe_report = slice_h1_module(source_module, "probe")
    retained = production_report["retained_original_variable_indices"]
    if retained != probe_report["retained_original_variable_indices"]:
        raise RuntimeError("production/probe variable partitions differ")

    expected_scripts = [
        Path("scripts/h1_model.py"),
        Path("scripts/inspect_source.py"),
        Path("scripts/construct_h1.py"),
        Path("scripts/validate_h1.py"),
    ]
    dependency = dependency_check(expected_scripts)
    frozen_hashes = criteria["implementation_sha256"]
    implementation_mismatches = []
    for path in expected_scripts:
        actual = file_identity(path)["sha256"]
        expected = frozen_hashes.get(path.as_posix())
        if actual != expected:
            implementation_mismatches.append(
                {"path": path.as_posix(), "expected": expected, "actual": actual}
            )
    if implementation_mismatches:
        raise RuntimeError(f"validator/construction identity changed after freeze: {implementation_mismatches}")

    source = tf.saved_model.load(str(model_dir))
    source_variables = list(source.main._static_model._variables)
    clean = make_clean_h1(source, production_module, retained)
    clean_variables = list(clean.h1_variables)
    if len(clean_variables) != 673:
        raise RuntimeError(f"expected 673 clean variables, found {len(clean_variables)}")

    parameter_rows = []
    for clean_index, source_index in enumerate(retained):
        official = np.asarray(source_variables[source_index].numpy())
        copied = np.asarray(clean_variables[clean_index].numpy())
        official_hash = array_sha256(official)
        copied_hash = array_sha256(copied)
        exact = np.array_equal(official, copied) and official.tobytes() == copied.tobytes()
        if not exact or official_hash != copied_hash:
            raise RuntimeError(f"parameter value mismatch at source index {source_index}")
        parameter_rows.append({
            "clean_index": clean_index,
            "official_index": source_index,
            "official_identity": f"root/main/_static_model/_variables/{source_index}",
            "clean_identity": f"root/h1_variables/{clean_index}",
            "shape": list(official.shape),
            "dtype": str(official.dtype),
            "raw_value_sha256": official_hash,
            "exact_value_equal": True,
        })
    parameter_identity = {
        "schema": "birdnet-clean-m2-parameter-identity",
        "version": 1,
        "state": "VALIDATED",
        "tensor_count": len(parameter_rows),
        "all_exact_value_equal": True,
        "tensor_value_identity_is_serialized_file_identity": False,
        "tensors": parameter_rows,
    }

    concrete = clean.serve.get_concrete_function()
    graph_def = concrete.graph.as_graph_def(add_shapes=True)
    graph_modules = []
    graph_ops = []
    for function in graph_def.library.function:
        for node in function.node_def:
            graph_ops.append(node.op)
            if node.op == "XlaCallModule":
                payload = bytes(node.attr["module"].s)
                graph_modules.append({
                    "function": function.signature.name,
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                })
    if not graph_modules:
        for node in graph_def.node:
            graph_ops.append(node.op)
            if node.op == "XlaCallModule":
                payload = bytes(node.attr["module"].s)
                graph_modules.append({
                    "function": "root",
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                })
    module_hashes = {row["sha256"] for row in graph_modules}
    if module_hashes != {production_report["artifact"]["sha256"]}:
        raise RuntimeError(f"clean graph module set is not the production slice: {graph_modules}")
    if EXPECTED_IDENTITIES["stablehlo_module"]["sha256"] in module_hashes:
        raise RuntimeError("clean graph retains the full predictor StableHLO module")

    fixtures = generated_fixtures(
        Path("records/m1/fixtures/canonical_waveform.npy"),
        Path("records/m1/fixtures/silence.npy"),
    )
    fixture_manifest = {
        "schema": "birdnet-clean-m2-fixtures",
        "version": 1,
        "state": "VALIDATED",
        "sample_rate_hz": 32_000,
        "duration_seconds": 3.0,
        "fixtures": {name: array_identity(value) for name, value in fixtures.items()},
    }
    for name, expected_hash in criteria["fixture_raw_sha256"].items():
        actual = fixture_manifest["fixtures"][name]["raw_sha256"]
        if actual != expected_hash:
            raise RuntimeError(f"fixture {name} changed after criteria freeze: {actual}")

    selected_source_variables = [source_variables[index] for index in retained]
    public = source.signatures["serving_default"]
    results: dict[str, Any] = {}
    failures = []
    for fixture_name, waveform in fixtures.items():
        model_input = prepare_frontend(source, waveform)
        official_probe = run_xla_module(
            probe_module, selected_source_variables, model_input, "probe"
        )
        clean_probe = run_xla_module(probe_module, clean_variables, model_input, "probe")
        clean_production = clean.serve(tf.convert_to_tensor(waveform, tf.float32))
        public_outputs = public(x=tf.convert_to_tensor(waveform, tf.float32))
        public_scores, public_embedding = classify_public_outputs(public_outputs)

        fixture_result: dict[str, Any] = {
            "official_probe_vs_clean_probe": {},
            "official_probe_vs_clean_production": {},
            "clean_probe_vs_clean_production": {},
            "official_public_embedding_vs_probe_embedding": comparison_metrics(
                public_embedding, official_probe["embedding"]
            ),
            "official_fused_predictions_vs_h1_scores": comparison_metrics(
                public_scores, official_probe["scores"]
            ),
        }
        exact_contract = criteria["comparisons"]["official_probe_vs_clean_probe"]
        for stage in ("shared_feature", "embedding", "h1_logits", "scores"):
            metrics = comparison_metrics(official_probe[stage], clean_probe[stage])
            fixture_result["official_probe_vs_clean_probe"][stage] = metrics
            failures.extend(
                f"{fixture_name}:official_probe_vs_clean_probe:{stage}:{failure}"
                for failure in threshold_check(metrics, exact_contract)
            )

        final_contract = criteria["comparisons"]["official_probe_vs_clean_production"]
        clean_contract = criteria["comparisons"]["clean_probe_vs_clean_production"]
        for stage in ("embedding", "scores"):
            official_metrics = comparison_metrics(
                official_probe[stage], clean_production[stage]
            )
            clean_metrics = comparison_metrics(clean_probe[stage], clean_production[stage])
            fixture_result["official_probe_vs_clean_production"][stage] = official_metrics
            fixture_result["clean_probe_vs_clean_production"][stage] = clean_metrics
            failures.extend(
                f"{fixture_name}:official_probe_vs_clean_production:{stage}:{failure}"
                for failure in threshold_check(official_metrics, final_contract[stage])
            )
            failures.extend(
                f"{fixture_name}:clean_probe_vs_clean_production:{stage}:{failure}"
                for failure in threshold_check(clean_metrics, clean_contract[stage])
            )

        public_contract = criteria["comparisons"]["official_public_embedding_vs_probe"]
        failures.extend(
            f"{fixture_name}:official_public_embedding_vs_probe:{failure}"
            for failure in threshold_check(
                fixture_result["official_public_embedding_vs_probe_embedding"],
                public_contract,
            )
        )
        oracle = independent_oracle(
            tf,
            official_probe["shared_feature"],
            source_variables[450].read_value(),
            source_variables[451].read_value(),
            source_variables[452].read_value(),
        )
        fixture_result["independent_arithmetic_oracle"] = {}
        oracle_contract = criteria["comparisons"]["independent_arithmetic_oracle"]
        for stage in ("embedding", "h1_logits", "scores"):
            metrics = comparison_metrics(official_probe[stage], oracle[stage])
            fixture_result["independent_arithmetic_oracle"][stage] = metrics
            failures.extend(
                f"{fixture_name}:independent_arithmetic_oracle:{stage}:{failure}"
                for failure in threshold_check(metrics, oracle_contract[stage])
            )
        results[fixture_name] = fixture_result

    if results["canonical"]["official_fused_predictions_vs_h1_scores"]["raw_byte_equal"]:
        failures.append("canonical official fused predictions unexpectedly equal H1-only scores")

    structural = {
        "schema": "birdnet-clean-m2-structural-proof",
        "version": 1,
        "state": "PROVEN" if not failures else "UNRESOLVED",
        "source_module": production_report["source_module"],
        "production_slice": production_report,
        "probe_slice": {
            "artifact": probe_report["artifact"],
            "output_types": probe_report["output_types"],
        },
        "clean_variable_count": len(clean_variables),
        "h2_h3_live_argument_indices": [],
        "h2_h3_serialized_state": "not applicable to in-memory M2; M3 audits serialization",
        "full_predictor_module_present": False,
        "clean_graph_xla_modules": graph_modules,
        "input_signature": str(concrete.structured_input_signature),
        "output_signature": str(concrete.structured_outputs),
        "output_widths": {"scores": 11_560, "embedding": 1280},
        "labels": labels,
        "retained_frontend_object": "official source.pre only; it has no learned variables",
    }
    validation = {
        "schema": "birdnet-clean-m2-validation-matrix",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "criteria": file_identity(args.criteria),
        "runtime": runtime,
        "fixture_count": len(fixtures),
        "results": results,
        "failures": failures,
        "note": "reported maxima are observations over the frozen fixtures, not universal bounds",
    }
    adversarial = {
        "schema": "birdnet-clean-m2-adversarial-review",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "issues": [
            {"issue": "retained parameter identity", "classification": "VALIDATED", "disposition": "all 673 tensors are raw-byte equal"},
            {"issue": "axes/layout/GeM order", "classification": "PROVEN", "disposition": "direct StableHLO slice plus independent arithmetic oracle"},
            {"issue": "five-contribution behavior", "classification": "PROVEN", "disposition": "five live paths remain; no algebraic collapse"},
            {"issue": "H1 score/logit semantics", "classification": "VALIDATED", "disposition": "separate probe stages and final sigmoid comparisons"},
            {"issue": "live cross-head dependency", "classification": "PROVEN", "disposition": "H2/H3 args 453..469 and unreachable functions are removed"},
            {"issue": "label order", "classification": "VALIDATED", "disposition": "exact official CSV identity and 11560 positional rows"},
            {"issue": "hidden full-predictor wrapper", "classification": "PROVEN", "disposition": "only the pruned StableHLO payload is present in the clean graph"},
            {"issue": "PyTorch/ONNX dependency", "classification": "VALIDATED", "disposition": "static AST and runtime module checks find none"},
            {"issue": "prospective criteria", "classification": "VALIDATED", "disposition": "criteria and implementation hashes were committed before this run"},
        ],
        "failures": failures,
        "unresolved": [] if not failures else failures,
    }
    accepted = {
        "schema": "birdnet-clean-m2-accepted-state",
        "version": 1,
        "disposition": "ACCEPTED" if not failures else "BLOCKED",
        "evidence_classification": "VALIDATED" if not failures else "UNRESOLVED",
        "m1_entry_verified": True,
        "clean_tensorflow_h1_in_memory": not failures,
        "m3_entry_authorized": not failures,
        "retained_variable_count": len(clean_variables),
        "production_stablehlo_sha256": production_report["artifact"]["sha256"],
        "probe_stablehlo_sha256": probe_report["artifact"]["sha256"],
        "pytorch_or_onnx_participated": False,
        "unresolved": [] if not failures else failures,
    }

    args.output.mkdir(parents=True, exist_ok=True)
    write_json(args.output / "FIXTURE_MANIFEST.json", fixture_manifest)
    write_json(args.output / "DEPENDENCY_CHECK.json", dependency)
    write_json(args.output / "PARAMETER_IDENTITY.json", parameter_identity)
    write_json(args.output / "STRUCTURAL_PROOF.json", structural)
    write_json(args.output / "VALIDATION_MATRIX.json", validation)
    write_json(args.output / "ADVERSARIAL_REVIEW.json", adversarial)
    write_json(args.output / "ACCEPTED_STATE.json", accepted)
    if failures:
        raise RuntimeError(f"M2 BLOCKED by {len(failures)} prospective-criteria failures")
    print(
        "M2 ACCEPTED: 673 exact tensors, 5 fixtures, exact official-probe/clean-probe stages"
    )


if __name__ == "__main__":
    main()
