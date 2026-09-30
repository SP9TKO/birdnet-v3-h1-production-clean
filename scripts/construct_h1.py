#!/usr/bin/env python3
"""Construct the clean TensorFlow FP32 H1 module from official TensorFlow bytes."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from h1_model import (
    EXPECTED_IDENTITIES,
    assert_runtime_environment,
    configure_tensorflow,
    embedded_stablehlo,
    file_identity,
    find_saved_model,
    make_clean_h1,
    parse_saved_model,
    slice_h1_module,
    tree_identity,
    write_json,
)


def normalize_fingerprint(saved_model_dir: Path) -> dict:
    """Replace TensorFlow's random fingerprint UUID with a content-derived UUID."""
    from tensorflow.core.protobuf import fingerprint_pb2

    path = saved_model_dir / "fingerprint.pb"
    fingerprint = fingerprint_pb2.FingerprintDef()
    fingerprint.ParseFromString(path.read_bytes())
    source_fields = {
        "saved_model_checksum": int(fingerprint.saved_model_checksum),
        "graph_def_program_hash": int(fingerprint.graph_def_program_hash),
        "signature_def_hash": int(fingerprint.signature_def_hash),
        "saved_object_graph_hash": int(fingerprint.saved_object_graph_hash),
        "checkpoint_hash": int(fingerprint.checkpoint_hash),
        "producer": int(fingerprint.version.producer),
    }
    seed = "\n".join(f"{name}={source_fields[name]}" for name in sorted(source_fields))
    deterministic_uuid = str(int.from_bytes(hashlib.sha256(seed.encode()).digest()[:16], "big"))
    fingerprint.uuid = deterministic_uuid
    path.write_bytes(fingerprint.SerializeToString(deterministic=True))
    return {
        "state": "VALIDATED",
        "reason": "TensorFlow 2.21 emits a random UUID while all semantic fingerprint fields are content-derived",
        "algorithm": "decimal unsigned big-endian integer from first 16 bytes of SHA-256 over sorted semantic fingerprint fields",
        "semantic_fields": source_fields,
        "deterministic_uuid": deterministic_uuid,
        "fingerprint_pb_sha256": file_identity(path)["sha256"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extracted", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output-saved-model", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    tf = configure_tensorflow()
    runtime = assert_runtime_environment(tf)
    model_dir = find_saved_model(args.extracted)
    source_pb = file_identity(model_dir / "saved_model.pb")
    if source_pb["sha256"] != EXPECTED_IDENTITIES["saved_model.pb"]["sha256"]:
        raise RuntimeError(f"official source mismatch: {source_pb}")

    saved_model = parse_saved_model(model_dir)
    source_module, occurrences = embedded_stablehlo(saved_model)
    production_module, production_report = slice_h1_module(source_module, "production")
    probe_module, probe_report = slice_h1_module(source_module, "probe")
    if production_report["retained_original_variable_indices"] != probe_report[
        "retained_original_variable_indices"
    ]:
        raise RuntimeError("production and validation probe slices retain different variables")

    args.work.mkdir(parents=True, exist_ok=True)
    production_path = args.work / "h1_production.mlirbc"
    probe_path = args.work / "h1_probe.mlirbc"
    production_path.write_bytes(production_module)
    probe_path.write_bytes(probe_module)
    write_json(args.work / "H1_PRODUCTION_SLICE.json", production_report)
    write_json(args.work / "H1_PROBE_SLICE.json", probe_report)

    source = tf.saved_model.load(str(model_dir))
    retained = production_report["retained_original_variable_indices"]
    clean = make_clean_h1(source, production_module, retained)
    signature = clean.serve.get_concrete_function()
    report = {
        "schema": "birdnet-clean-h1-construction",
        "version": 1,
        "state": "VALIDATED",
        "source_saved_model_pb": source_pb,
        "source_stablehlo_occurrences": occurrences,
        "production_slice": production_report,
        "probe_slice": {
            "artifact": probe_report["artifact"],
            "output_types": probe_report["output_types"],
            "retained_variable_count": probe_report["retained_variable_count"],
        },
        "retained_variable_count": len(retained),
        "retained_original_variable_indices": retained,
        "in_memory_signature": {
            "input": str(signature.structured_input_signature),
            "output": str(signature.structured_outputs),
        },
        "runtime": runtime,
        "construction_script": file_identity(Path(__file__)),
        "pytorch_or_onnx_participated": False,
    }

    if args.output_saved_model is not None:
        if args.output_saved_model.exists():
            raise RuntimeError(
                f"refusing to overwrite existing serialized artifact: {args.output_saved_model}"
            )
        args.output_saved_model.parent.mkdir(parents=True, exist_ok=True)
        tf.saved_model.save(
            clean,
            str(args.output_saved_model),
            signatures={"serving_default": signature},
        )
        report["fingerprint_normalization"] = normalize_fingerprint(
            args.output_saved_model
        )
        report["serialized_artifact"] = tree_identity(args.output_saved_model)
        report["serialized_artifact"]["source_stablehlo_sha256"] = hashlib.sha256(
            source_module
        ).hexdigest()

    write_json(args.report, report)
    print(
        "constructed clean H1: "
        f"variables={len(retained)} production={production_report['artifact']['sha256']}"
        + (
            f" SavedModel={report['serialized_artifact']['tree_sha256']}"
            if "serialized_artifact" in report
            else " in-memory only"
        )
    )


if __name__ == "__main__":
    main()
