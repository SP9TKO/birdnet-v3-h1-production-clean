#!/usr/bin/env python3
"""Future-only constructor for the prospectively frozen N1 M4 bundle.

The deployment composite is a four-component bundle.  Frontend, backbone, and
classifier bytes are regenerated from canonical M3; the solved SE-MEAN
emission is applied to the regenerated backbone; GeM is copied byte-for-byte.
This script is frozen now and intentionally cannot run without an explicit
post-freeze authorization guard.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
from typing import Any

import numpy as np

from h1_model import assert_runtime_environment, configure_tensorflow
from m4_componentized_candidate_common import convert_component
from m4_componentized_common import make_components, save_components, slice_stages, source_binding
from m4_n1_freeze_common import (
    CLASSIFIER_INPUT_SCALE_BITS,
    CLASSIFIER_RANGE_ANCHOR_BITS,
    GEM_BYTES,
    GEM_PATH,
    GEM_SHA256,
    LABELS_SHA256,
    M2_STABLEHLO_SHA256,
    M3_TREE_SHA256,
    file_identity,
    float32_bits,
    float32_from_bits,
    require_file,
    write_json,
)
from m4_n1_se_mean_emit import (
    CONTRACT_PATH,
    CONTRACT_SHA256,
    TOPOLOGY_PATH,
    TOPOLOGY_SHA256,
    authorize_execution,
    emit_standard_means,
)


COMPONENT_ORDER = ("frontend", "backbone", "gem", "classifier")


def load_bank(directory: Path, count: int) -> list[np.ndarray]:
    paths = sorted(directory.glob("*.npy"))
    if len(paths) != count:
        raise RuntimeError(f"{directory} contains {len(paths)} rows, expected {count}")
    return [np.load(path, allow_pickle=False).astype(np.float32, copy=False) for path in paths]


def classifier_calibration_with_range_anchor(rows: list[np.ndarray]) -> list[np.ndarray]:
    """Preserve all calibration members and append the frozen design-range anchor."""

    anchor_value = float32_from_bits(CLASSIFIER_RANGE_ANCHOR_BITS)
    scale = float32_from_bits(CLASSIFIER_INPUT_SCALE_BITS)
    if np.float32(anchor_value / np.float32(32767)).tobytes() != scale.tobytes():
        raise RuntimeError("classifier range anchor no longer produces the frozen scale")
    if any(np.max(np.abs(row), initial=np.float32(0.0)) > anchor_value for row in rows):
        raise RuntimeError("frozen calibration row exceeds the design-range anchor")
    anchor = np.zeros((1, 1280), dtype=np.float32)
    anchor[0, 0] = anchor_value
    return [*rows, anchor]


def write_once(path: Path, payload: bytes) -> dict[str, Any]:
    if path.exists():
        raise RuntimeError(f"refusing to overwrite {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return file_identity(path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--saved-model", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--reference-scratch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execution", choices=("A", "B"), required=True)
    parser.add_argument("--freeze-commit", required=True)
    parser.add_argument("--execute-frozen-experiment", action="store_true")
    args = parser.parse_args()
    authorize_execution(args.execute_frozen_experiment, args.freeze_commit)
    if args.output.exists():
        raise RuntimeError(f"refusing to reuse output directory {args.output}")

    tf = configure_tensorflow()
    runtime = assert_runtime_environment(tf)
    source, stablehlo_payload = source_binding(args.saved_model, args.labels)
    if source["canonical_m3_saved_model"]["tree_sha256"] != M3_TREE_SHA256:
        raise RuntimeError("canonical M3 tree changed")
    if source["official_labels"]["sha256"] != LABELS_SHA256:
        raise RuntimeError("official labels changed")
    if {item["sha256"] for item in source["production_h1_stablehlo"]} != {M2_STABLEHLO_SHA256}:
        raise RuntimeError("M2 production StableHLO changed")
    gem_identity = require_file(GEM_PATH, GEM_SHA256, GEM_BYTES)
    contract_identity = require_file(CONTRACT_PATH, CONTRACT_SHA256)
    contract = json.loads(CONTRACT_PATH.read_text())
    topology_identity = require_file(TOPOLOGY_PATH, TOPOLOGY_SHA256)
    topology = json.loads(TOPOLOGY_PATH.read_text())

    backbone_module, backbone_slice = slice_stages(stablehlo_payload, ("shared_feature",))
    loaded = tf.saved_model.load(str(args.saved_model))
    components = make_components(loaded, backbone_module, backbone_slice, tf)
    component_sources = save_components(components, args.output / "saved-model-components", tf)

    calibration_frontend = load_bank(args.reference_scratch / "calibration_frontend", 128)
    calibration_embedding = load_bank(args.reference_scratch / "calibration_embedding", 128)
    frontend_bytes = convert_component(
        "frontend", args.output / "saved-model-components" / "frontend", None, tf
    )
    raw_backbone = convert_component(
        "backbone",
        args.output / "saved-model-components" / "backbone",
        calibration_frontend,
        tf,
    )
    backbone_bytes, se_mean_report = emit_standard_means(raw_backbone, contract, topology)
    classifier_bytes = convert_component(
        "classifier",
        args.output / "saved-model-components" / "classifier",
        classifier_calibration_with_range_anchor(calibration_embedding),
        tf,
    )

    artifacts = {
        "frontend": write_once(args.output / "frontend.tflite", frontend_bytes),
        "backbone": write_once(args.output / "backbone.tflite", backbone_bytes),
        "classifier": write_once(args.output / "classifier.tflite", classifier_bytes),
    }
    gem_destination = args.output / "gem.tflite"
    if gem_destination.exists():
        raise RuntimeError(f"refusing to overwrite {gem_destination}")
    shutil.copyfile(GEM_PATH, gem_destination)
    artifacts["gem"] = require_file(gem_destination, GEM_SHA256, GEM_BYTES)
    artifacts = {name: artifacts[name] for name in COMPONENT_ORDER}

    result = {
        "schema": "birdnet-clean-m4-n1-composition-emission",
        "version": 1,
        "state": "EMITTED_UNQUALIFIED",
        "execution": args.execution,
        "freeze_commit": args.freeze_commit,
        "source_binding": source,
        "runtime": runtime,
        "backbone_stablehlo_slice": backbone_slice,
        "component_saved_models": component_sources,
        "artifacts": artifacts,
        "gem_reuse": {"state": "REUSE_BYTE_EXACT", "source": gem_identity},
        "se_mean_emission": se_mean_report,
        "se_mean_contract": contract_identity,
        "se_mean_topology": topology_identity,
        "classifier_range_anchor": {
            "value": float(float32_from_bits(CLASSIFIER_RANGE_ANCHOR_BITS)),
            "bits": float32_bits(float32_from_bits(CLASSIFIER_RANGE_ANCHOR_BITS)),
            "expected_scale_bits": float32_bits(float32_from_bits(CLASSIFIER_INPUT_SCALE_BITS)),
            "placement": "one additional [1,1280] representative row, channel 0; all other entries zero",
            "calibration_waveform_members_changed": False,
            "qualification_data_used": False,
        },
        "composition": [
            "waveform FP32 -> frontend FP32",
            "quantize to INT16 -> A16W8 backbone with 30 builtin INT16 MEAN operators",
            "dequantize shared_feature INT16 -> exact reused FP32 GeM",
            "quantize embedding to INT16 -> A16W8 classifier",
            "dequantize logits -> FP32 sigmoid -> scores in official label order",
        ],
        "qualified": False,
        "m5_started": False,
    }
    write_json(args.output / "EMISSION.json", result)


if __name__ == "__main__":
    main()
