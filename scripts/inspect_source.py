#!/usr/bin/env python3
"""Verify the official TensorFlow source and mechanically establish H1."""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path

import numpy as np

from h1_model import (
    ARCHIVE_NAME,
    EXPECTED_IDENTITIES,
    FULL_TFLITE_NAME,
    LABELS_NAME,
    array_identity,
    assert_runtime_environment,
    configure_tensorflow,
    embedded_stablehlo,
    file_identity,
    find_saved_model,
    inspect_h1_payload,
    inspect_object_graph_binding,
    parse_saved_model,
    slice_h1_module,
    write_json,
)


def verify_identity(path: Path, expected: dict, *, include_md5: bool) -> dict:
    actual = file_identity(path, md5=include_md5)
    mismatches = {
        key: {"expected": value, "actual": actual.get(key)}
        for key, value in expected.items()
        if actual.get(key) != value
    }
    if mismatches:
        raise RuntimeError(f"identity mismatch for {path}: {mismatches}")
    actual["state"] = "VALIDATED"
    return actual


def label_contract(path: Path) -> dict:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    if not rows:
        raise RuntimeError("official label CSV is empty")
    header = [cell.strip().lower() for cell in rows[0]]
    header_present = any(
        token in cell
        for cell in header
        for token in ("scientific", "common", "label", "class")
    )
    data = rows[1:] if header_present else rows
    if len(data) != 11_560:
        raise RuntimeError(f"expected 11560 positional label rows, found {len(data)}")
    encoded_rows = "\n".join(",".join(row) for row in data).encode("utf-8")
    return {
        "state": "VALIDATED",
        "ordering_authority": "row order in exact official release CSV",
        "header_present": header_present,
        "header": rows[0] if header_present else None,
        "data_rows": len(data),
        "ordered_rows_sha256": hashlib.sha256(encoded_rows).hexdigest(),
        "first_data_row": data[0],
        "last_data_row": data[-1],
        "positional_mapping": "classifier output column i maps to official CSV data row i",
    }


def signature_record(function) -> dict:
    concrete = function.concrete_functions[0]
    return {
        "input": str(concrete.structured_input_signature),
        "output": str(concrete.structured_outputs),
        "graph_input_count": len(concrete.graph.inputs),
        "captured_input_count": len(concrete.captured_inputs),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--extracted", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    tf = configure_tensorflow()
    runtime = assert_runtime_environment(tf)

    archive = verify_identity(
        args.official / ARCHIVE_NAME,
        EXPECTED_IDENTITIES[ARCHIVE_NAME],
        include_md5=True,
    )
    labels = verify_identity(
        args.official / LABELS_NAME,
        EXPECTED_IDENTITIES[LABELS_NAME],
        include_md5=True,
    )
    labels["contract"] = label_contract(args.official / LABELS_NAME)

    full_tflite_path = args.official / FULL_TFLITE_NAME
    if full_tflite_path.exists():
        tflite = verify_identity(
            full_tflite_path,
            EXPECTED_IDENTITIES[FULL_TFLITE_NAME],
            include_md5=True,
        )
        tflite["role"] = "corroboration only; not read by accepted M1-M3 construction"
    else:
        tflite = {
            "state": "OBSERVED",
            "role": "corroboration only; not acquired and not read by this rework",
            "expected_identity": EXPECTED_IDENTITIES[FULL_TFLITE_NAME],
        }

    model_dir = find_saved_model(args.extracted)
    saved_model_pb = verify_identity(
        model_dir / "saved_model.pb",
        EXPECTED_IDENTITIES["saved_model.pb"],
        include_md5=False,
    )
    saved_model = parse_saved_model(model_dir)
    payload, occurrences = embedded_stablehlo(saved_model)
    h1_contract, canonical_types = inspect_h1_payload(payload)
    production_module, production_slice = slice_h1_module(payload, "production")

    loaded = tf.saved_model.load(str(model_dir))
    binding_input = dict(h1_contract)
    binding_input["_canonical_argument_types"] = canonical_types
    binding = inspect_object_graph_binding(saved_model, loaded, binding_input)
    variables = list(loaded.main._static_model._variables)
    gem_value = np.asarray(variables[450].numpy())
    classifier_weight = np.asarray(variables[451].numpy())
    classifier_bias = np.asarray(variables[452].numpy())
    h1_contract.update({
        "tensorflow_object_graph_binding": binding,
        "learned_parameter_identity": {
            "gem_exponent": {
                "object_path": "root/main/_static_model/_variables/450",
                "stablehlo_argument": 450,
                "value": [float(value) for value in gem_value],
                **array_identity(gem_value),
            },
            "classifier_weight": {
                "object_path": "root/main/_static_model/_variables/451",
                "stablehlo_argument": 451,
                **array_identity(classifier_weight),
            },
            "classifier_bias": {
                "object_path": "root/main/_static_model/_variables/452",
                "stablehlo_argument": 452,
                **array_identity(classifier_bias),
            },
        },
        "state_partition": {
            "shared_inference_variable_count": production_slice["retained_variable_count"] - 3,
            "h1_specific_variable_indices": [450, 451, 452],
            "h2_h3_only_variable_indices": list(range(453, 470)),
            "inference_dead_variable_indices": production_slice[
                "removed_inference_dead_variable_indices"
            ],
            "clean_h1_retained_variable_count": production_slice["retained_variable_count"],
        },
        "labels": labels["contract"],
        "forward_compatible_boundary": {
            "state": "PROVEN",
            "shared_feature_shape": [1, 7, 9, 1280],
            "layout": "NHWC",
            "ownership": "output of shared encoder; input to H1 GeM and separate H2/H3 paths",
            "production_interface_note": "not exported by the current deployment signature; retained as a mechanically addressable construction/probe boundary",
        },
    })

    source_identity = {
        "schema": "birdnet-clean-m1-authoritative-source",
        "version": 1,
        "state": "VALIDATED",
        "zenodo_record": "20703646",
        "doi": "10.5281/zenodo.20703646",
        "production_authority": "official TensorFlow FP32 Protobuf SavedModel",
        "archive": archive,
        "saved_model_pb": saved_model_pb,
        "stablehlo": {
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "occurrences": occurrences,
        },
        "variables_index": file_identity(model_dir / "variables" / "variables.index"),
        "variables_data": file_identity(
            model_dir / "variables" / "variables.data-00000-of-00001"
        ),
        "labels": labels,
        "full_fp32_tflite": tflite,
        "full_fp32_tflite_participated": False,
        "pytorch_or_onnx_participated": False,
    }

    source_public = loaded.signatures["serving_default"]
    signatures = {
        "schema": "birdnet-clean-m1-tensorflow-signatures",
        "version": 1,
        "state": "PROVEN",
        "public_serving_default": {
            "input": str(source_public.structured_input_signature),
            "output": str(source_public.structured_outputs),
            "note": "public predictions are fused H1/H2/H3 scores, not H1-only scores",
        },
        "frontend_serve": signature_record(loaded.pre.serve),
        "static_main_serve": signature_record(loaded.main.serve),
    }

    adversarial = {
        "schema": "birdnet-clean-m1-adversarial-review",
        "version": 1,
        "state": "VALIDATED",
        "question": "Could an index, axis, dependency, layout, GeM, label, evaluation, or producer-source assumption make the H1 contract wrong?",
        "issues": [
            {
                "issue": "variable-index assumption",
                "classification": "PROVEN",
                "disposition": "object-graph list order, concrete capture identity, and all 806 StableHLO argument types align mechanically",
            },
            {
                "issue": "StableHLO axis interpretation",
                "classification": "PROVEN",
                "disposition": "explicit NHWC-to-NCHW transpose and reduction dimensions [3,2] over 7x9 are present",
            },
            {
                "issue": "hidden H2/H3 dependency",
                "classification": "PROVEN",
                "disposition": "backward slice excludes arguments 453..469 and all unreachable H2/H3 operations",
            },
            {
                "issue": "classifier layout assumption",
                "classification": "PROVEN",
                "disposition": "stored [11560,1280] tensor is explicitly transposed to [1280,11560] before each matrix multiply",
            },
            {
                "issue": "GeM identification",
                "classification": "PROVEN",
                "disposition": "arg450 feeds both powers through reciprocal; exact chain returns the public embedding operand",
            },
            {
                "issue": "five-contribution evaluation behavior",
                "classification": "PROVEN",
                "disposition": "five live classifier paths share exact weight/bias, contain no stochastic op, are ordered-summed and divided by five",
            },
            {
                "issue": "H1 score versus fused prediction semantics",
                "classification": "PROVEN",
                "disposition": "the only released final sigmoid follows 0.4/0.35/0.25 fusion; clean H1 applies that exact sigmoid helper to unweighted H1 logits",
            },
            {
                "issue": "label-position assumption",
                "classification": "VALIDATED",
                "disposition": "exact official CSV identity, stable row ordering and 11560-row/output-width agreement define the release's positional contract",
            },
            {
                "issue": "producer-source inference",
                "classification": "VALIDATED",
                "disposition": "no producer implementation is required; all accepted semantics come from the released TensorFlow object",
            },
        ],
        "unresolved": [],
    }

    accepted = {
        "schema": "birdnet-clean-m1-accepted-state",
        "version": 1,
        "disposition": "ACCEPTED",
        "authority": "OFFICIAL_TENSORFLOW_FP32_SAVEDMODEL",
        "evidence_classification": "PROVEN",
        "h1_boundary_mechanically_established": True,
        "m2_entry_authorized": True,
        "unresolved": [],
        "source_archive_sha256": archive["sha256"],
        "saved_model_pb_sha256": saved_model_pb["sha256"],
        "stablehlo_sha256": hashlib.sha256(payload).hexdigest(),
        "labels_sha256": labels["sha256"],
        "production_slice_sha256": hashlib.sha256(production_module).hexdigest(),
    }

    write_json(args.output / "SOURCE_IDENTITY.json", source_identity)
    write_json(args.output / "RUNTIME_ENVIRONMENT.json", runtime)
    write_json(args.output / "TENSORFLOW_SIGNATURES.json", signatures)
    write_json(args.output / "H1_CONTRACT.json", h1_contract)
    write_json(args.output / "ADVERSARIAL_REVIEW.json", adversarial)
    write_json(args.output / "ACCEPTED_STATE.json", accepted)
    print(
        f"M1 ACCEPTED: source={archive['sha256']} StableHLO={accepted['stablehlo_sha256']} "
        f"retained_variables={production_slice['retained_variable_count']}"
    )


if __name__ == "__main__":
    main()
