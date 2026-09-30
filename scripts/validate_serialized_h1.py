#!/usr/bin/env python3
"""Independently reload and audit the canonical serialized TensorFlow H1 model."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

import numpy as np

from h1_model import (
    EMBEDDING_SHAPE,
    EXPECTED_IDENTITIES,
    SCORES_SHAPE,
    array_sha256,
    artifact_xla_modules,
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
    tree_identity,
    write_json,
)
from validate_h1 import dependency_check, label_check, threshold_check


def load_json(path: Path) -> Any:
    return json.loads(path.read_text())


def signature_audit(function) -> dict[str, Any]:
    inputs = function.structured_input_signature
    outputs = function.structured_outputs
    keyword_inputs = inputs[1]
    if set(keyword_inputs) != {"waveform"}:
        raise RuntimeError(f"unexpected serialized input names: {keyword_inputs}")
    waveform = keyword_inputs["waveform"]
    if waveform.shape.as_list() != [1, 96_000] or waveform.dtype.name != "float32":
        raise RuntimeError(f"unexpected serialized waveform contract: {waveform}")
    if set(outputs) != {"scores", "embedding"}:
        raise RuntimeError(f"unexpected serialized output names: {outputs}")
    if outputs["scores"].shape.as_list() != SCORES_SHAPE or outputs["scores"].dtype.name != "float32":
        raise RuntimeError(f"unexpected serialized score contract: {outputs['scores']}")
    if outputs["embedding"].shape.as_list() != EMBEDDING_SHAPE or outputs["embedding"].dtype.name != "float32":
        raise RuntimeError(f"unexpected serialized embedding contract: {outputs['embedding']}")
    return {
        "state": "VALIDATED",
        "input": {"name": "waveform", "shape": [1, 96_000], "dtype": "float32"},
        "outputs": {
            "scores": {"shape": SCORES_SHAPE, "dtype": "float32"},
            "embedding": {"shape": EMBEDDING_SHAPE, "dtype": "float32"},
        },
        "structured_signature_summary": {
            "positional_input_count": len(inputs[0]),
            "keyword_input_names": sorted(keyword_inputs),
            "output_names": sorted(outputs),
        },
    }


def checkpoint_audit(tf, artifact: Path) -> dict[str, Any]:
    prefix = artifact / "variables" / "variables"
    listed = tf.train.list_variables(str(prefix))
    if len(listed) != 674:
        raise RuntimeError(f"expected object graph plus 673 variables, found {len(listed)}")
    names = [name for name, _ in listed if name != "_CHECKPOINTABLE_OBJECT_GRAPH"]
    pattern = re.compile(r"^h1_variables/(\d+)/\.ATTRIBUTES/VARIABLE_VALUE$")
    indices = []
    for name in names:
        match = pattern.match(name)
        if match is None:
            raise RuntimeError(f"unexpected serialized checkpoint tensor: {name}")
        indices.append(int(match.group(1)))
    if sorted(indices) != list(range(673)):
        raise RuntimeError("serialized checkpoint variable indices are not exactly 0..672")
    return {
        "state": "PROVEN",
        "checkpoint_entry_count": len(listed),
        "learned_state_tensor_count": len(names),
        "checkpoint_object_graph_entries": 1,
        "tensor_namespace": "h1_variables/0..672 only",
        "dead_serialized_learned_state_count": 0,
        "h2_h3_serialized_learned_state_count": 0,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extracted", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--rebuild-artifact", type=Path, required=True)
    parser.add_argument("--build-report", type=Path, required=True)
    parser.add_argument("--rebuild-report", type=Path, required=True)
    parser.add_argument("--criteria", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    criteria = load_json(args.criteria)
    if criteria.get("state") != "VALIDATED" or not criteria.get("frozen_before_results"):
        raise RuntimeError("M3 criteria are not prospectively frozen")
    m2_state_path = Path("records/m2/ACCEPTED_STATE.json")
    if file_identity(m2_state_path)["sha256"] != criteria["m2_accepted_state_sha256"]:
        raise RuntimeError("accepted M2 entry record changed after M3 criteria freeze")
    m2_state = load_json(m2_state_path)
    if m2_state.get("disposition") != "ACCEPTED" or not m2_state.get("m3_entry_authorized"):
        raise RuntimeError("M3 entry gate is not authorized by accepted M2")

    tf = configure_tensorflow()
    runtime = assert_runtime_environment(tf)
    labels = label_check(args.labels)

    implementation_paths = [
        Path("scripts/h1_model.py"),
        Path("scripts/construct_h1.py"),
        Path("scripts/validate_h1.py"),
        Path("scripts/validate_serialized_h1.py"),
    ]
    dependency = dependency_check(implementation_paths)
    dependency["schema"] = "birdnet-clean-m3-dependency-check"
    dependency["conclusion"] = (
        "no PyTorch or ONNX package is imported or executed by accepted M1-M3"
    )
    identity_mismatches = []
    for path in implementation_paths:
        actual = file_identity(path)["sha256"]
        expected = criteria["implementation_sha256"].get(path.as_posix())
        if actual != expected:
            identity_mismatches.append(
                {"path": path.as_posix(), "expected": expected, "actual": actual}
            )
    if identity_mismatches:
        raise RuntimeError(f"M3 implementation changed after freeze: {identity_mismatches}")

    canonical_tree = tree_identity(args.artifact)
    rebuild_tree = tree_identity(args.rebuild_artifact)
    if canonical_tree["tree_sha256"] != rebuild_tree["tree_sha256"]:
        raise RuntimeError(
            "independent serialization tree identities differ: "
            f"{canonical_tree['tree_sha256']} != {rebuild_tree['tree_sha256']}"
        )
    canonical_rows = [
        (row["relative_path"], row["bytes"], row["sha256"])
        for row in canonical_tree["files"]
    ]
    rebuild_rows = [
        (row["relative_path"], row["bytes"], row["sha256"])
        for row in rebuild_tree["files"]
    ]
    if canonical_rows != rebuild_rows:
        raise RuntimeError("independent serialization file identities differ")

    build_report = load_json(args.build_report)
    rebuild_report = load_json(args.rebuild_report)
    for name, report, tree in [
        ("canonical", build_report, canonical_tree),
        ("rebuild", rebuild_report, rebuild_tree),
    ]:
        recorded = report.get("serialized_artifact", {}).get("tree_sha256")
        if recorded != tree["tree_sha256"]:
            raise RuntimeError(f"{name} build report does not bind its artifact: {recorded}")
        if report["construction_script"]["sha256"] != criteria["implementation_sha256"][
            "scripts/construct_h1.py"
        ]:
            raise RuntimeError(f"{name} build used an unfrozen construction script")

    model_dir = find_saved_model(args.extracted)
    source_saved_model = parse_saved_model(model_dir)
    source_module, _ = embedded_stablehlo(source_saved_model)
    production_module, production_report = slice_h1_module(source_module, "production")
    probe_module, probe_report = slice_h1_module(source_module, "probe")
    expected_production = criteria["production_stablehlo_sha256"]
    if production_report["artifact"]["sha256"] != expected_production:
        raise RuntimeError("production StableHLO changed after M3 criteria freeze")

    canonical_modules = artifact_xla_modules(args.artifact)
    rebuild_modules = artifact_xla_modules(args.rebuild_artifact)
    for name, rows in [("canonical", canonical_modules), ("rebuild", rebuild_modules)]:
        hashes = {row["sha256"] for row in rows}
        if hashes != {expected_production}:
            raise RuntimeError(f"{name} contains unexpected XlaCallModule payloads: {rows}")
        if EXPECTED_IDENTITIES["stablehlo_module"]["sha256"] in hashes:
            raise RuntimeError(f"{name} retains the full predictor module")

    # These loads occur in a different process from both construction commands.
    source = tf.saved_model.load(str(model_dir))
    canonical = tf.saved_model.load(str(args.artifact))
    rebuild = tf.saved_model.load(str(args.rebuild_artifact))
    if "main" in canonical._trackable_children() or "main" in rebuild._trackable_children():
        raise RuntimeError("serialized H1 retains a full-model main child")
    canonical_variables = list(canonical.h1_variables)
    rebuild_variables = list(rebuild.h1_variables)
    if len(canonical_variables) != 673 or len(rebuild_variables) != 673:
        raise RuntimeError(
            f"serialized variable counts changed: {len(canonical_variables)}, {len(rebuild_variables)}"
        )
    if len(getattr(canonical.frontend, "variables", [])) != 0:
        raise RuntimeError("serialized frontend unexpectedly owns learned variables")

    signatures = sorted(canonical.signatures.keys())
    if signatures != ["serving_default"]:
        raise RuntimeError(f"unexpected canonical signature set: {signatures}")
    signature = canonical.signatures["serving_default"]
    rebuild_signature = rebuild.signatures["serving_default"]
    signature_record = signature_audit(signature)
    signature_audit(rebuild_signature)
    canonical_checkpoint = checkpoint_audit(tf, args.artifact)
    rebuild_checkpoint = checkpoint_audit(tf, args.rebuild_artifact)

    m2_parameters_path = Path("records/m2/PARAMETER_IDENTITY.json")
    m2_parameters = load_json(m2_parameters_path)
    if m2_parameters["tensor_count"] != 673:
        raise RuntimeError("M2 parameter manifest count changed")
    source_variables = list(source.main._static_model._variables)
    digest = hashlib.sha256()
    parameter_mismatches = []
    for row in m2_parameters["tensors"]:
        clean_index = row["clean_index"]
        source_index = row["official_index"]
        expected_hash = row["raw_value_sha256"]
        observed = {
            "official": array_sha256(source_variables[source_index].numpy()),
            "canonical": array_sha256(canonical_variables[clean_index].numpy()),
            "rebuild": array_sha256(rebuild_variables[clean_index].numpy()),
        }
        if set(observed.values()) != {expected_hash}:
            parameter_mismatches.append({
                "clean_index": clean_index,
                "source_index": source_index,
                "expected": expected_hash,
                "observed": observed,
            })
        digest.update(
            f"{clean_index} {source_index} {row['dtype']} {row['shape']} {expected_hash}\n".encode()
        )
    if parameter_mismatches:
        raise RuntimeError(f"serialized parameter mismatch: {parameter_mismatches[:3]}")
    parameter_audit = {
        "state": "VALIDATED",
        "source_manifest": {
            **file_identity(m2_parameters_path),
            "tensor_count": 673,
        },
        "checked_tensor_count": 673,
        "exact_official_canonical_rebuild_value_matches": 673,
        "mismatch_count": 0,
        "ordered_parameter_contract_sha256": digest.hexdigest(),
    }

    retained = production_report["retained_original_variable_indices"]
    clean = make_clean_h1(source, production_module, retained)
    clean_variables = list(clean.h1_variables)
    selected_source_variables = [source_variables[index] for index in retained]
    fixtures = generated_fixtures(
        Path("records/m1/fixtures/canonical_waveform.npy"),
        Path("records/m1/fixtures/silence.npy"),
    )
    failures = []
    matrix: dict[str, Any] = {}
    for name, waveform in fixtures.items():
        actual_fixture_hash = array_sha256(waveform)
        expected_fixture_hash = criteria["fixture_raw_sha256"].get(name)
        if actual_fixture_hash != expected_fixture_hash:
            raise RuntimeError(f"M3 fixture {name} changed: {actual_fixture_hash}")
        model_input = prepare_frontend(source, waveform)
        official_probe = run_xla_module(
            probe_module, selected_source_variables, model_input, "probe"
        )
        clean_probe = run_xla_module(probe_module, clean_variables, model_input, "probe")
        clean_production = clean.serve(tf.convert_to_tensor(waveform, tf.float32))
        canonical_output = signature(waveform=tf.convert_to_tensor(waveform, tf.float32))
        rebuild_output = rebuild_signature(
            waveform=tf.convert_to_tensor(waveform, tf.float32)
        )
        row: dict[str, Any] = {
            "A_official_vs_B_clean_probe": {},
            "A_official_vs_B_clean_production": {},
            "A_official_vs_C_reloaded": {},
            "B_clean_vs_C_reloaded": {},
            "C_canonical_vs_independent_rebuild": {},
        }
        exact = criteria["comparisons"]["A_official_vs_B_clean_probe"]
        for stage in ("shared_feature", "embedding", "h1_logits", "scores"):
            metrics = comparison_metrics(official_probe[stage], clean_probe[stage])
            row["A_official_vs_B_clean_probe"][stage] = metrics
            failures.extend(
                f"{name}:A_vs_B_probe:{stage}:{failure}"
                for failure in threshold_check(metrics, exact)
            )
        for stage in ("embedding", "scores"):
            comparisons = {
                "A_official_vs_B_clean_production": comparison_metrics(
                    official_probe[stage], clean_production[stage]
                ),
                "A_official_vs_C_reloaded": comparison_metrics(
                    official_probe[stage], canonical_output[stage]
                ),
                "B_clean_vs_C_reloaded": comparison_metrics(
                    clean_production[stage], canonical_output[stage]
                ),
                "C_canonical_vs_independent_rebuild": comparison_metrics(
                    canonical_output[stage], rebuild_output[stage]
                ),
            }
            for comparison_name, metrics in comparisons.items():
                row[comparison_name][stage] = metrics
                contract = criteria["comparisons"][comparison_name][stage]
                failures.extend(
                    f"{name}:{comparison_name}:{stage}:{failure}"
                    for failure in threshold_check(metrics, contract)
                )
        matrix[name] = row

    artifact_identity = {
        "schema": "birdnet-clean-m3-artifact-identity",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "path": args.artifact.as_posix(),
        "format": "TensorFlow SavedModel",
        "canonical_tree": canonical_tree,
        "independent_rebuild_path": args.rebuild_artifact.as_posix(),
        "independent_rebuild_tree_sha256": rebuild_tree["tree_sha256"],
        "byte_deterministic_independent_rebuild": True,
        "tensorflow": tf.__version__,
        "construction_script": file_identity(Path("scripts/construct_h1.py")),
        "official_source": {
            "saved_model_pb_sha256": EXPECTED_IDENTITIES["saved_model.pb"]["sha256"],
            "stablehlo_sha256": EXPECTED_IDENTITIES["stablehlo_module"]["sha256"],
        },
        "labels_sha256": labels["identity"]["sha256"],
    }
    audit = {
        "schema": "birdnet-clean-m3-serialized-audit",
        "version": 1,
        "state": "PROVEN" if not failures else "UNRESOLVED",
        "independent_reload": True,
        "constructor_process_released_before_reload": True,
        "trackable_children": sorted(canonical._trackable_children()),
        "signatures": signature_record,
        "checkpoint": canonical_checkpoint,
        "rebuild_checkpoint": rebuild_checkpoint,
        "xla_modules": canonical_modules,
        "full_predictor_module_present": False,
        "h2_h3_live_computation_present": False,
        "h2_h3_serialized_learned_state_present": False,
        "parameter_audit": parameter_audit,
        "labels": labels,
        "production_stablehlo_sha256": expected_production,
    }
    validation = {
        "schema": "birdnet-clean-m3-reload-validation",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "roles": {
            "A": "official TensorFlow source validation probe",
            "B": "clean in-memory TensorFlow H1",
            "C": "independently reloaded canonical TensorFlow SavedModel",
        },
        "criteria": file_identity(args.criteria),
        "runtime": runtime,
        "results": matrix,
        "failures": failures,
        "note": "reported maxima are observations over the frozen fixtures, not universal bounds",
    }
    adversarial = {
        "schema": "birdnet-clean-m3-adversarial-review",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "issues": [
            {"issue": "serialization completeness", "classification": "VALIDATED", "disposition": "signature, checkpoint, graph payload and all output fixtures reload"},
            {"issue": "loss or change of variables", "classification": "VALIDATED", "disposition": "673 official/canonical/rebuild raw value hashes match"},
            {"issue": "hidden full-model state", "classification": "PROVEN", "disposition": "checkpoint namespace is only h1_variables/0..672 and the full module hash is absent"},
            {"issue": "reload behavior", "classification": "VALIDATED", "disposition": "independent public-API reload passes the frozen A/B/C matrix"},
            {"issue": "output ordering and semantics", "classification": "VALIDATED", "disposition": "named scores/embedding signatures have exact dtype and shape"},
            {"issue": "label ordering and source identity", "classification": "VALIDATED", "disposition": "exact M1 authority and labels identities are bound"},
            {"issue": "reconstruction determinism", "classification": "PROVEN", "disposition": "two independent reconstruction runs using the same deterministic constructor emitted byte-identical four-file SavedModel trees"},
            {"issue": "PyTorch/ONNX dependency", "classification": "VALIDATED", "disposition": "static and runtime checks find none"},
        ],
        "failures": failures,
        "unresolved": [] if not failures else failures,
    }
    binding = {
        "schema": "CANONICAL_M4_FP32_INPUT",
        "version": 1,
        "state": "VALIDATED" if not failures else "UNRESOLVED",
        "path": args.artifact.as_posix(),
        "format": "TensorFlow SavedModel",
        "tree_sha256": canonical_tree["tree_sha256"],
        "tree_identity_algorithm": canonical_tree["aggregate_algorithm"],
        "file_count": canonical_tree["file_count"],
        "total_bytes": canonical_tree["total_bytes"],
        "production_stablehlo_sha256": expected_production,
        "source_saved_model_pb_sha256": EXPECTED_IDENTITIES["saved_model.pb"]["sha256"],
        "source_stablehlo_sha256": EXPECTED_IDENTITIES["stablehlo_module"]["sha256"],
        "labels_sha256": labels["identity"]["sha256"],
        "substitution_permitted": False,
    }
    accepted = {
        "schema": "birdnet-clean-m3-accepted-state",
        "version": 1,
        "disposition": "ACCEPTED" if not failures else "BLOCKED",
        "evidence_classification": "VALIDATED" if not failures else "UNRESOLVED",
        "m2_entry_verified": True,
        "canonical_saved_model_tree_sha256": canonical_tree["tree_sha256"],
        "independent_rebuild_tree_sha256": rebuild_tree["tree_sha256"],
        "canonical_m4_binding_written": not failures,
        "m4_entry_authorized": not failures,
        "m4_started": False,
        "pytorch_or_onnx_participated": False,
        "unresolved": [] if not failures else failures,
    }

    args.output.mkdir(parents=True, exist_ok=True)
    write_json(args.output / "ARTIFACT_IDENTITY.json", artifact_identity)
    write_json(args.output / "SERIALIZED_AUDIT.json", audit)
    write_json(args.output / "RELOAD_VALIDATION.json", validation)
    write_json(args.output / "DEPENDENCY_CHECK.json", dependency)
    write_json(args.output / "ADVERSARIAL_REVIEW.json", adversarial)
    write_json(args.output / "CANONICAL_M4_FP32_INPUT.json", binding)
    write_json(args.output / "ACCEPTED_STATE.json", accepted)
    if failures:
        raise RuntimeError(f"M3 BLOCKED by {len(failures)} prospective-criteria failures")
    print(
        f"M3 ACCEPTED: canonical SavedModel tree={canonical_tree['tree_sha256']} "
        "with a byte-identical second run of the same deterministic constructor"
    )


if __name__ == "__main__":
    main()
