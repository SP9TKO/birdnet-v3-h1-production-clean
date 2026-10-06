#!/usr/bin/env python3
"""Shared construction helpers for the prospectively componentized M4 gate."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from h1_model import (
    CANONICAL_INPUT_SHAPE,
    EMBEDDING_SHAPE,
    SCORES_SHAPE,
    SHARED_FEATURE_SHAPE,
    STABLEHLO_TARGET_VERSION,
    _call_name,
    _dependencies,
    _function_map,
    _opview,
    _owner_operation,
    _reachable_functions,
    _register_mlir,
    array_identity,
    array_sha256,
    file_identity,
    generated_fixtures,
    parse_saved_model,
    tree_identity,
)


M3_TREE_SHA256 = "4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a"
M3_PRODUCTION_STABLEHLO_SHA256 = (
    "3cff5699feedb7cde9c877f88ecaefc9c73da44fb735875411f5fdd10f78ecda"
)
LABELS_SHA256 = "8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0"
FRONTEND_SHAPE = [1, 224, 281, 3]
M3_GEM_VARIABLE_INDEX = 450
M3_CLASSIFIER_WEIGHT_INDEX = 451
M3_CLASSIFIER_BIAS_INDEX = 452
BANNED_DEPENDENCIES = {"onnx", "onnxruntime", "torch", "torchaudio", "torchvision"}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text())


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def verify_criteria(path: Path, expected_schema: str) -> dict[str, Any]:
    criteria = load_json(path)
    if criteria.get("schema") != expected_schema:
        raise RuntimeError(f"unexpected criteria schema in {path}: {criteria.get('schema')}")
    if criteria.get("state") != "VALIDATED" or not criteria.get("frozen_before_results"):
        raise RuntimeError(f"criteria are not prospectively frozen: {path}")
    for implementation, expected in criteria["implementation_sha256"].items():
        actual = file_identity(Path(implementation))["sha256"]
        if actual != expected:
            raise RuntimeError(
                f"implementation hash mismatch for {implementation}: {actual} != {expected}"
            )
    return criteria


def source_binding(saved_model: Path, labels: Path) -> tuple[dict[str, Any], bytes]:
    saved_identity = tree_identity(saved_model)
    label_identity = file_identity(labels)
    if saved_identity["tree_sha256"] != M3_TREE_SHA256:
        raise RuntimeError(f"canonical M3 tree mismatch: {saved_identity['tree_sha256']}")
    if label_identity["sha256"] != LABELS_SHA256:
        raise RuntimeError(f"official labels mismatch: {label_identity['sha256']}")

    model = parse_saved_model(saved_model)
    occurrences = []
    payloads: dict[str, bytes] = {}
    for function in model.meta_graphs[0].graph_def.library.function:
        for node in function.node_def:
            if node.op != "XlaCallModule":
                continue
            payload = bytes(node.attr["module"].s)
            digest = sha256_bytes(payload)
            payloads[digest] = payload
            occurrences.append(
                {
                    "function": function.signature.name,
                    "node": node.name,
                    "bytes": len(payload),
                    "sha256": digest,
                    "version": int(node.attr["version"].i),
                }
            )
    if set(payloads) != {M3_PRODUCTION_STABLEHLO_SHA256}:
        raise RuntimeError(f"canonical M3 StableHLO mismatch: {sorted(payloads)}")
    return (
        {
            "schema": "birdnet-clean-m4-componentized-source-binding",
            "version": 1,
            "state": "VALIDATED",
            "canonical_m3_saved_model": saved_identity,
            "production_h1_stablehlo": occurrences,
            "official_labels": label_identity,
            "source_substitution_permitted": False,
            "historical_model_artifacts_participated": False,
            "pytorch_or_onnx_participated": False,
        },
        payloads[M3_PRODUCTION_STABLEHLO_SHA256],
    )


def _trace_stage_values(module: Any) -> dict[str, Any]:
    from jaxlib.mlir.dialects import func

    main = _function_map(module)["main"]
    block = main.body.blocks[0]
    terminator = _opview(list(block.operations)[-1])
    if not isinstance(terminator, func.ReturnOp) or len(terminator.operands) != 2:
        raise RuntimeError("canonical M3 main return changed")
    scores, embedding = terminator.operands
    sigmoid = _opview(_owner_operation(scores))
    if _call_name(sigmoid) != "_aten_sigmoid_83dd7e2f":
        raise RuntimeError(f"canonical M3 score producer changed: {_call_name(sigmoid)}")
    logits = sigmoid.operands[0]
    value = embedding
    chain = []
    for _ in range(7):
        producer = _opview(_owner_operation(value))
        chain.append(_call_name(producer) or producer.operation.name)
        value = producer.operands[0]
    shared = value
    expected_chain = [
        "_aten_unsafe_view_a3dfae78",
        "_aten_pow_5f058cbf",
        "_aten_mean_abe74551",
        "_aten_pow_9f8f1ed6",
        "_aten_clamp_8ecb06ea",
        "_aten_clone_568b95fc",
        "permute_65f95971",
    ]
    expected_types = {
        "shared_feature": "tensor<1x7x9x1280xf32>",
        "embedding": "tensor<1x1280xf32>",
        "h1_logits": "tensor<1x11560xf32>",
        "scores": "tensor<1x11560xf32>",
    }
    actual_types = {
        "shared_feature": str(shared.type),
        "embedding": str(embedding.type),
        "h1_logits": str(logits.type),
        "scores": str(scores.type),
    }
    if chain != expected_chain or actual_types != expected_types:
        raise RuntimeError(
            f"canonical M3 stage trace changed: chain={chain} types={actual_types}"
        )
    return {
        "main": main,
        "block": block,
        "terminator": terminator,
        "values": {
            "shared_feature": shared,
            "embedding": embedding,
            "h1_logits": logits,
            "scores": scores,
        },
        "gem_chain_from_embedding_backwards": chain,
    }


def slice_stages(payload: bytes, names: tuple[str, ...]) -> tuple[bytes, dict[str, Any]]:
    """Return a DCE'd StableHLO slice with the requested canonical M3 stages."""
    from jaxlib.mlir import ir
    from jaxlib.mlir.dialects import func, stablehlo

    allowed = {"shared_feature", "embedding", "h1_logits", "scores"}
    if not names or set(names) - allowed:
        raise ValueError(f"unsupported stage slice: {names}")
    context = _register_mlir()
    with context:
        module = stablehlo.deserialize_portable_artifact(context, payload)
        traced = _trace_stage_values(module)
        main = traced["main"]
        block = traced["block"]
        old_return = traced["terminator"]
        original_arguments = list(block.arguments)
        outputs = [traced["values"][name] for name in names]
        with ir.InsertionPoint(old_return):
            new_return = func.ReturnOp(outputs, loc=old_return.location)
        old_return.operation.erase()

        keep = _dependencies(outputs)
        keep.add(new_return.operation)
        for operation in reversed(list(block.operations)):
            if operation not in keep:
                operation.erase()

        retained = [
            index
            for index, argument in enumerate(original_arguments)
            if len(list(argument.uses)) > 0
        ]
        if not retained or retained[-1] != len(original_arguments) - 1:
            raise RuntimeError("canonical model input was not retained as the final argument")
        for index in reversed(range(len(original_arguments))):
            if index not in retained:
                block.erase_argument(index)

        main.attributes["function_type"] = ir.TypeAttr.get(
            ir.FunctionType.get(
                [argument.type for argument in block.arguments],
                [value.type for value in outputs],
            )
        )
        reachable = _reachable_functions(module)
        for operation in reversed(list(module.body.operations)):
            view = _opview(operation)
            if isinstance(view, func.FuncOp) and str(view.name).strip('"') not in reachable:
                operation.erase()
        if not module.operation.verify():
            raise RuntimeError(f"StableHLO stage slice failed verification: {names}")
        artifact = stablehlo.serialize_portable_artifact_str(
            str(module), STABLEHLO_TARGET_VERSION
        )

    return artifact, {
        "state": "PROVEN",
        "source_sha256": sha256_bytes(payload),
        "artifact": {"bytes": len(artifact), "sha256": sha256_bytes(artifact)},
        "outputs": list(names),
        "output_types": [str(value.type) for value in outputs],
        "retained_variable_positions": retained[:-1],
        "retained_variable_count": len(retained) - 1,
        "stablehlo_target_version": STABLEHLO_TARGET_VERSION,
        "gem_chain_from_embedding_backwards": traced[
            "gem_chain_from_embedding_backwards"
        ],
    }


def classifier_semantic_trace(payload: bytes) -> dict[str, Any]:
    """Mechanically prove the five canonical M3 H1 classifier contributions."""
    from jaxlib.mlir.dialects import func, stablehlo

    context = _register_mlir()
    with context:
        module = stablehlo.deserialize_portable_artifact(context, payload)
        traced = _trace_stage_values(module)
        block = traced["block"]
        embedding = traced["values"]["embedding"]
        logits = traced["values"]["h1_logits"]
        dependencies = _dependencies([logits])
        matrix_multiplications = []
        forbidden_stochastic = []
        for operation in block.operations:
            if operation not in dependencies:
                continue
            view = _opview(operation)
            name = _call_name(view) if isinstance(view, func.CallOp) else operation.name
            lowered = name.lower()
            if any(token in lowered for token in ("dropout", "random", "rng")):
                forbidden_stochastic.append(name)
            if isinstance(view, func.CallOp) and name.startswith("_aten_mm_"):
                matrix_multiplications.append(view)
        if len(matrix_multiplications) != 5:
            raise RuntimeError(
                f"expected five canonical H1 matrix multiplications, found {len(matrix_multiplications)}"
            )
        functions = _function_map(module)
        identity_input_helpers = []
        same_embedding = []
        for operation in matrix_multiplications:
            clone = _opview(_owner_operation(operation.operands[0]))
            clone_name = _call_name(clone)
            helper = functions.get(clone_name)
            helper_is_identity = False
            if isinstance(clone, func.CallOp) and helper is not None:
                helper_block = helper.body.blocks[0]
                helper_operations = list(helper_block.operations)
                helper_return = _opview(helper_operations[-1])
                helper_is_identity = (
                    len(clone.operands) == 1
                    and bool(clone.operands[0] == embedding)
                    and len(helper_block.arguments) == 1
                    and len(helper_operations) == 1
                    and isinstance(helper_return, func.ReturnOp)
                    and len(helper_return.operands) == 1
                    and bool(helper_return.operands[0] == helper_block.arguments[0])
                )
            identity_input_helpers.append(clone_name)
            same_embedding.append(helper_is_identity)
        arguments = list(block.arguments)
        weight_uses = sum(
            1
            for use in arguments[M3_CLASSIFIER_WEIGHT_INDEX].uses
            if (use.owner.operation if hasattr(use.owner, "operation") else use.owner)
            in dependencies
        )
        bias_uses = sum(
            1
            for use in arguments[M3_CLASSIFIER_BIAS_INDEX].uses
            if (use.owner.operation if hasattr(use.owner, "operation") else use.owner)
            in dependencies
        )
        divide = _opview(_owner_operation(logits))
        divide_name = _call_name(divide)
        divide_text = str(_function_map(module)[divide_name])
        if "stablehlo.divide" not in divide_text or "dense<5>" not in str(
            _owner_operation(divide.operands[1])
        ):
            raise RuntimeError("canonical H1 ordered average/divisor changed")
        if not all(same_embedding) or (weight_uses, bias_uses) != (5, 5):
            raise RuntimeError(
                "canonical H1 paths do not share embedding/weight/bias exactly: "
                f"embedding={same_embedding} weight={weight_uses} bias={bias_uses}"
            )
        if forbidden_stochastic:
            raise RuntimeError(f"stochastic operation remains in M3 H1 slice: {forbidden_stochastic}")
        return {
            "state": "PROVEN",
            "authority": "accepted clean M3 production StableHLO",
            "stablehlo_sha256": sha256_bytes(payload),
            "five_live_classifier_helpers_in_order": [
                _call_name(operation) for operation in matrix_multiplications
            ],
            "five_embedding_identity_helpers_in_order": identity_input_helpers,
            "identity_helper_semantics": "single block containing only return %arg0",
            "all_five_inputs_are_exact_same_embedding_value_through_identity_helpers": True,
            "shared_weight_argument_position": M3_CLASSIFIER_WEIGHT_INDEX,
            "shared_weight_use_count": weight_uses,
            "shared_bias_argument_position": M3_CLASSIFIER_BIAS_INDEX,
            "shared_bias_use_count": bias_uses,
            "stochastic_operations_in_inference_slice": 0,
            "aggregation": "ordered sum from float32 zeros then stablehlo.divide by scalar 5",
            "direct_average_removal_allowed": False,
        }


def frontend_value(loaded: Any, waveform: Any, tf: Any) -> Any:
    value = loaded.frontend.serve(tf.convert_to_tensor(waveform, tf.float32))
    if isinstance(value, dict):
        if len(value) != 1:
            raise RuntimeError(f"unexpected frontend mapping: {value.keys()}")
        value = next(iter(value.values()))
    if isinstance(value, (tuple, list)):
        if len(value) != 1:
            raise RuntimeError(f"unexpected frontend sequence length: {len(value)}")
        value = value[0]
    if value.shape.as_list() != FRONTEND_SHAPE or value.dtype != tf.float32:
        raise RuntimeError(f"unexpected frontend result: {value.shape}/{value.dtype}")
    return value


def run_stage_slice(
    loaded: Any,
    module: bytes,
    report: dict[str, Any],
    frontend: Any,
    tf: Any,
) -> dict[str, np.ndarray]:
    from tensorflow.compiler.tf2xla.python import xla

    variables = list(loaded.h1_variables)
    arguments = [
        variables[index].read_value()
        for index in report["retained_variable_positions"]
    ]
    arguments.append(tf.transpose(frontend, [0, 3, 1, 2]))
    shape_by_name = {
        "shared_feature": SHARED_FEATURE_SHAPE,
        "embedding": EMBEDDING_SHAPE,
        "h1_logits": SCORES_SHAPE,
        "scores": SCORES_SHAPE,
    }
    values = xla.call_module(
        arguments,
        version=5,
        module=module,
        Tout=[tf.float32] * len(report["outputs"]),
        Sout=[shape_by_name[name] for name in report["outputs"]],
    )
    return {
        name: np.asarray(value.numpy())
        for name, value in zip(report["outputs"], values)
    }


def make_components(
    loaded: Any,
    backbone_module: bytes,
    backbone_report: dict[str, Any],
    tf: Any,
) -> dict[str, Any]:
    """Create explicit TensorFlow modules using only canonical M3 values."""
    from tensorflow.compiler.tf2xla.python import xla

    source_variables = list(loaded.h1_variables)

    class FrontendComponent(tf.Module):
        def __init__(self) -> None:
            super().__init__(name="birdnet_v3_frontend_fp32")
            self.frontend = loaded.frontend
            self.serve = tf.function(
                self._serve,
                input_signature=[
                    tf.TensorSpec(CANONICAL_INPUT_SHAPE, tf.float32, name="waveform")
                ],
                autograph=False,
            )

        def _serve(self, waveform: Any) -> dict[str, Any]:
            return {"frontend": frontend_value(self, waveform, tf)}

    class BackboneComponent(tf.Module):
        def __init__(self) -> None:
            super().__init__(name="birdnet_v3_backbone_a16w8_source")
            self.variables_m3 = [
                tf.Variable(
                    source_variables[index].read_value(),
                    trainable=False,
                    name=f"m3_backbone_variable_{index:03d}",
                )
                for index in backbone_report["retained_variable_positions"]
            ]
            self._module = backbone_module
            self.serve = tf.function(
                self._serve,
                input_signature=[
                    tf.TensorSpec(FRONTEND_SHAPE, tf.float32, name="frontend")
                ],
                autograph=False,
            )

        def _serve(self, frontend: Any) -> dict[str, Any]:
            result = xla.call_module(
                [value.read_value() for value in self.variables_m3]
                + [tf.transpose(frontend, [0, 3, 1, 2])],
                version=5,
                module=self._module,
                Tout=[tf.float32],
                Sout=[SHARED_FEATURE_SHAPE],
            )[0]
            return {"shared_feature": result}

    class GemComponent(tf.Module):
        def __init__(self) -> None:
            super().__init__(name="birdnet_v3_h1_gem_fp32")
            self.p = tf.Variable(
                source_variables[M3_GEM_VARIABLE_INDEX].read_value(),
                trainable=False,
                name="m3_gem_exponent",
            )
            self.serve = tf.function(
                self._serve,
                input_signature=[
                    tf.TensorSpec(
                        SHARED_FEATURE_SHAPE, tf.float32, name="shared_feature"
                    )
                ],
                autograph=False,
            )

        def _serve(self, shared_feature: Any) -> dict[str, Any]:
            nchw = tf.transpose(shared_feature, [0, 3, 1, 2])
            clamped = tf.maximum(nchw, tf.constant(np.float32(1e-6)))
            exponent = tf.reshape(self.p.read_value(), [])
            powered = tf.math.pow(clamped, exponent)
            spatial_mean = tf.reduce_mean(powered, axis=[3, 2])
            inverse_exponent = tf.math.reciprocal(exponent)
            embedding = tf.reshape(
                tf.math.pow(spatial_mean, inverse_exponent), EMBEDDING_SHAPE
            )
            return {"embedding": embedding}

    class ClassifierComponent(tf.Module):
        def __init__(self) -> None:
            super().__init__(name="birdnet_v3_h1_classifier_a16w8_source")
            self.weight = tf.Variable(
                source_variables[M3_CLASSIFIER_WEIGHT_INDEX].read_value(),
                trainable=False,
                name="m3_h1_classifier_weight",
            )
            self.bias = tf.Variable(
                source_variables[M3_CLASSIFIER_BIAS_INDEX].read_value(),
                trainable=False,
                name="m3_h1_classifier_bias",
            )
            self.serve = tf.function(
                self._serve,
                input_signature=[
                    tf.TensorSpec(EMBEDDING_SHAPE, tf.float32, name="embedding")
                ],
                autograph=False,
            )

        def linear(self, embedding: Any) -> Any:
            return tf.nn.bias_add(
                tf.linalg.matmul(embedding, self.weight, transpose_b=True), self.bias
            )

        def _serve(self, embedding: Any) -> dict[str, Any]:
            classifier_result = self.linear(embedding)
            ordered_sum = tf.zeros_like(classifier_result)
            for _ in range(5):
                ordered_sum = tf.math.add(ordered_sum, classifier_result)
            logits = tf.math.divide(
                ordered_sum, tf.constant(np.float32(5.0), dtype=tf.float32)
            )
            return {"h1_logits": logits}

    return {
        "frontend": FrontendComponent(),
        "backbone": BackboneComponent(),
        "gem": GemComponent(),
        "classifier": ClassifierComponent(),
    }


def explicit_five_call_classifier(
    embedding: Any, weight: Any, bias: Any, tf: Any
) -> Any:
    def linear() -> Any:
        return tf.nn.bias_add(tf.linalg.matmul(embedding, weight, transpose_b=True), bias)

    ordered_sum = tf.zeros_like(linear())
    for _ in range(5):
        ordered_sum = tf.math.add(ordered_sum, linear())
    return tf.math.divide(
        ordered_sum, tf.constant(np.float32(5.0), dtype=tf.float32)
    )


def official_reuse_pattern_classifier(
    embedding: Any, weight: Any, bias: Any, tf: Any
) -> Any:
    """Reproduce the official TFLite's one-FC/four-ADD/0.2 pattern."""
    result = tf.nn.bias_add(
        tf.linalg.matmul(embedding, weight, transpose_b=True), bias
    )
    ordered_sum = tf.math.add(result, result)
    for _ in range(3):
        ordered_sum = tf.math.add(ordered_sum, result)
    return tf.math.multiply(
        ordered_sum, tf.constant(np.float32(0.2), dtype=tf.float32)
    )


def save_components(
    components: dict[str, Any], root: Path, tf: Any
) -> dict[str, Any]:
    from construct_h1 import normalize_fingerprint

    root.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {}
    for name, component in components.items():
        destination = root / name
        if destination.exists():
            raise RuntimeError(f"refusing to overwrite component SavedModel: {destination}")
        signature = component.serve.get_concrete_function()
        tf.saved_model.save(
            component,
            str(destination),
            signatures={"serving_default": signature},
        )
        fingerprint = normalize_fingerprint(destination)
        result[name] = {
            "tree": tree_identity(destination),
            "fingerprint_normalization": fingerprint,
            "signature": {
                "input": str(signature.structured_input_signature),
                "output": str(signature.structured_outputs),
            },
        }
    return result


def graph_inventory(concrete: Any) -> dict[str, Any]:
    counts: dict[str, int] = {}
    for operation in concrete.graph.get_operations():
        counts[operation.type] = counts.get(operation.type, 0) + 1
    return {
        "operation_count": sum(counts.values()),
        "operation_counts": dict(sorted(counts.items())),
        "input_signature": str(concrete.structured_input_signature),
        "outputs": str(concrete.structured_outputs),
    }


def generated_waveforms(seed: int, count: int) -> list[np.ndarray]:
    rng = np.random.Generator(np.random.PCG64(seed))
    sample_count = CANONICAL_INPUT_SHAPE[1]
    time = np.arange(sample_count, dtype=np.float64) / 32_000.0
    rows = []
    for index in range(count):
        waveform = np.zeros(sample_count, dtype=np.float64)
        for _ in range(1 + index % 4):
            frequency = rng.uniform(60, 14_500)
            sweep = rng.uniform(-1_800, 1_800)
            phase = rng.uniform(-math.pi, math.pi)
            amplitude = rng.uniform(0.05, 0.55)
            waveform += amplitude * np.sin(
                2 * math.pi * (frequency * time + 0.5 * (sweep / 3.0) * time * time)
                + phase
            )
        waveform += (10.0 ** rng.uniform(-5, -1.3)) * rng.standard_normal(sample_count)
        if index % 11 == 0:
            locations = rng.choice(sample_count, 8, replace=False)
            waveform[locations] += rng.uniform(-1, 1, 8)
        maximum = np.max(np.abs(waveform), initial=0.0)
        peak = 10.0 ** rng.uniform(-4, math.log10(0.95))
        if maximum:
            waveform *= peak / maximum
        rows.append(np.clip(waveform, -1, 1).astype(np.float32)[None, :])
    return rows


def array_manifest(seed: int | str, rows: Iterable[np.ndarray]) -> dict[str, Any]:
    digest = hashlib.sha256()
    entries = []
    for index, row in enumerate(rows):
        identity = array_sha256(row)
        digest.update(f"{index:04d} {identity}\n".encode())
        entries.append({"index": index, "sha256": identity})
    return {
        "seed": seed,
        "count": len(entries),
        "aggregate_sha256": digest.hexdigest(),
        "rows": entries,
    }


def fixture_corpora(criteria: dict[str, Any]) -> tuple[list[np.ndarray], dict[str, np.ndarray], dict[str, Any]]:
    contract = criteria["corpora"]
    calibration = generated_waveforms(
        contract["calibration_seed"], contract["calibration_count"]
    )
    generated_qualification = generated_waveforms(
        contract["qualification_seed"], contract["qualification_count"]
    )
    calibration_manifest = array_manifest(contract["calibration_seed"], calibration)
    qualification_manifest = array_manifest(
        contract["qualification_seed"], generated_qualification
    )
    if (
        calibration_manifest["aggregate_sha256"]
        != contract["expected_calibration_waveform_aggregate_sha256"]
        or qualification_manifest["aggregate_sha256"]
        != contract["expected_qualification_waveform_aggregate_sha256"]
    ):
        raise RuntimeError("prospectively frozen waveform generator identity changed")
    accepted = generated_fixtures(
        Path("records/m1/fixtures/canonical_waveform.npy"),
        Path("records/m1/fixtures/silence.npy"),
    )
    qualification = dict(accepted)
    qualification.update(
        {
            f"generated_{index:04d}": value
            for index, value in enumerate(generated_qualification)
        }
    )
    calibration_hashes = {array_sha256(value) for value in calibration}
    generated_hashes = {array_sha256(value) for value in generated_qualification}
    accepted_hashes = {array_sha256(value) for value in accepted.values()}
    overlap = {
        "calibration_generated_qualification": sorted(
            calibration_hashes & generated_hashes
        ),
        "calibration_accepted_qualification": sorted(
            calibration_hashes & accepted_hashes
        ),
        "generated_accepted_qualification": sorted(generated_hashes & accepted_hashes),
    }
    if any(overlap.values()):
        raise RuntimeError(f"calibration/qualification overlap: {overlap}")
    return calibration, qualification, {
        "schema": "birdnet-clean-m4-componentized-corpus-manifest",
        "version": 1,
        "state": "VALIDATED",
        "generator": contract["generator"],
        "calibration_waveforms": calibration_manifest,
        "generated_qualification_waveforms": qualification_manifest,
        "accepted_qualification_fixtures": {
            name: array_identity(value) for name, value in accepted.items()
        },
        "disjointness": {
            "state": "PROVEN",
            "overlap": overlap,
            "five_accepted_fixtures_are_qualification_only": True,
        },
        "historical_tensor_payload_used": False,
    }


def boundary_manifest(name: str, rows: Iterable[np.ndarray]) -> dict[str, Any]:
    rows = list(rows)
    manifest = array_manifest(name, rows)
    finite = all(bool(np.isfinite(value).all()) for value in rows)
    return {
        "semantic_boundary": name,
        "aggregate_sha256": manifest["aggregate_sha256"],
        "count": manifest["count"],
        "rows": manifest["rows"],
        "finite": finite,
        "minimum": float(min(np.min(value) for value in rows)),
        "maximum": float(max(np.max(value) for value in rows)),
    }


def numerical_metrics(reference: Any, candidate: Any) -> dict[str, Any]:
    left = np.asarray(reference, dtype=np.float64)
    right = np.asarray(candidate, dtype=np.float64)
    if left.shape != right.shape:
        raise RuntimeError(f"numerical shape mismatch: {left.shape} != {right.shape}")
    difference = np.abs(left - right)
    rmse = float(np.sqrt(np.mean(difference * difference)))
    reference_rms = float(np.sqrt(np.mean(left * left)))
    return {
        "shape": list(left.shape),
        "elements": int(left.size),
        "max_absolute_error": float(difference.max(initial=0.0)),
        "mean_absolute_error": float(difference.mean()),
        "rmse": rmse,
        "reference_rms": reference_rms,
        "rmse_over_reference_rms": float(rmse / max(reference_rms, 1e-12)),
        "finite": bool(np.isfinite(left).all() and np.isfinite(right).all()),
        "raw_byte_equal": bool(
            np.ascontiguousarray(reference).tobytes()
            == np.ascontiguousarray(candidate).tobytes()
        ),
    }


def metric_failures(metrics: dict[str, Any], limits: dict[str, float]) -> list[str]:
    failures = []
    if not metrics["finite"]:
        failures.append("nonfinite values observed")
    for name, limit in limits.items():
        if metrics[name] > limit:
            failures.append(f"{name}={metrics[name]} exceeds {limit}")
    return failures
