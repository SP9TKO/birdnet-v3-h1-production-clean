#!/usr/bin/env python3
"""Shared TensorFlow-native H1 construction and evidence helpers.

This module deliberately contains no PyTorch or ONNX dependency.  StableHLO is
used only as the released TensorFlow SavedModel's embedded computation format.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any, Iterable

import numpy as np


ARCHIVE_NAME = "BirdNET+_V3.0-preview3.1_Global_11K_FP32_Protobuf.zip"
LABELS_NAME = "BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv"
FULL_TFLITE_NAME = "BirdNET+_V3.0-preview3.1_Global_11K_FP32.tflite"
EXPECTED_IDENTITIES = {
    ARCHIVE_NAME: {
        "bytes": 499_609_919,
        "sha256": "ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb",
        "md5": "f0409a35ac3e1605cbe6190e0c81bba9",
    },
    LABELS_NAME: {
        "bytes": 809_172,
        "sha256": "8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0",
        "md5": "21ccf9e984a955bfc2d5c52faab6a6ea",
    },
    FULL_TFLITE_NAME: {
        "bytes": 540_471_440,
        "sha256": "a932aea50ec90984467e4c6da3d4d7bcc6a650a40e8bf35cf084219fef69fcd7",
        "md5": "d7fe09a7d7ffd786e470e655c55b95f6",
    },
    "saved_model.pb": {
        "bytes": 3_870_764,
        "sha256": "4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e",
    },
    "stablehlo_module": {
        "bytes": 313_747,
        "sha256": "f8f5968213692c2a4b17e5b504736a601fc60b4b3203bab51bc1c5cc8b8c2dac",
    },
}
EXPECTED_RUNTIME_ENV = {
    "PYTHONHASHSEED": "0",
    "TF_DETERMINISTIC_OPS": "1",
    "TF_ENABLE_ONEDNN_OPTS": "0",
    "TF_NUM_INTRAOP_THREADS": "1",
    "TF_NUM_INTEROP_THREADS": "1",
    "OMP_NUM_THREADS": "1",
    "MKL_NUM_THREADS": "1",
    "OPENBLAS_NUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
    "CUDA_VISIBLE_DEVICES": "",
}
STABLEHLO_TARGET_VERSION = "1.13.1"
CANONICAL_INPUT_SHAPE = [1, 96_000]
PREPROCESSED_INPUT_SHAPE = [1, 3, 224, 281]
SHARED_FEATURE_SHAPE = [1, 7, 9, 1280]
SCORES_SHAPE = [1, 11_560]
EMBEDDING_SHAPE = [1, 1280]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def array_sha256(value: Any) -> str:
    return sha256_bytes(np.ascontiguousarray(np.asarray(value)).tobytes())


def file_identity(path: Path, *, md5: bool = False) -> dict[str, Any]:
    h256 = hashlib.sha256()
    hmd5 = hashlib.md5() if md5 else None
    size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            size += len(chunk)
            h256.update(chunk)
            if hmd5 is not None:
                hmd5.update(chunk)
    result: dict[str, Any] = {
        "path": path.as_posix(),
        "bytes": size,
        "sha256": h256.hexdigest(),
    }
    if hmd5 is not None:
        result["md5"] = hmd5.hexdigest()
    return result


def tree_identity(root: Path) -> dict[str, Any]:
    files = []
    aggregate = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        identity = file_identity(path)
        identity["relative_path"] = path.relative_to(root).as_posix()
        identity.pop("path")
        files.append(identity)
        aggregate.update(
            f"{identity['sha256']}  {identity['bytes']}  {identity['relative_path']}\n".encode()
        )
    return {
        "format": "TensorFlow SavedModel directory",
        "root": root.as_posix(),
        "file_count": len(files),
        "total_bytes": sum(row["bytes"] for row in files),
        "aggregate_algorithm": "sha256 of sorted '<file_sha256>  <bytes>  <relative_path>\\n' rows",
        "tree_sha256": aggregate.hexdigest(),
        "files": files,
    }


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def find_saved_model(extracted: Path) -> Path:
    if (extracted / "saved_model.pb").is_file():
        return extracted
    hits = sorted(extracted.rglob("saved_model.pb"))
    if len(hits) == 1:
        return hits[0].parent
    combined = [path for path in hits if "combined" in {part.lower() for part in path.parts}]
    if len(combined) == 1:
        return combined[0].parent
    raise RuntimeError(
        "could not uniquely select the released combined waveform SavedModel: "
        f"hits={len(hits)} combined={combined}"
    )


def configure_tensorflow():
    import tensorflow as tf

    tf.config.threading.set_intra_op_parallelism_threads(1)
    tf.config.threading.set_inter_op_parallelism_threads(1)
    tf.config.optimizer.set_experimental_options({
        "constant_folding": False,
        "disable_meta_optimizer": True,
    })
    return tf


def assert_runtime_environment(tf) -> dict[str, Any]:
    import sys

    problems = []
    actual_env = {name: os.environ.get(name) for name in EXPECTED_RUNTIME_ENV}
    for name, expected in EXPECTED_RUNTIME_ENV.items():
        if actual_env[name] != expected:
            problems.append(f"{name}={actual_env[name]!r}, expected {expected!r}")
    if sys.version_info[:3] != (3, 12, 14):
        problems.append(f"Python={sys.version.split()[0]}, expected 3.12.14")
    if tf.__version__ != "2.21.0":
        problems.append(f"TensorFlow={tf.__version__}, expected 2.21.0")
    if np.__version__ != "2.2.6":
        problems.append(f"NumPy={np.__version__}, expected 2.2.6")
    options = tf.config.optimizer.get_experimental_options()
    if options.get("constant_folding") is not False:
        problems.append(f"constant_folding={options.get('constant_folding')!r}, expected False")
    if options.get("disable_meta_optimizer") is not True:
        problems.append(
            f"disable_meta_optimizer={options.get('disable_meta_optimizer')!r}, expected True"
        )
    if problems:
        raise RuntimeError("runtime contract mismatch: " + "; ".join(problems))
    return {
        "state": "VALIDATED",
        "python": sys.version,
        "python_executable": file_identity(Path(sys.executable)),
        "tensorflow": tf.__version__,
        "numpy": np.__version__,
        "environment": actual_env,
        "optimizer_experimental_options": options,
        "threading": {
            "intra_op": tf.config.threading.get_intra_op_parallelism_threads(),
            "inter_op": tf.config.threading.get_inter_op_parallelism_threads(),
        },
    }


def parse_saved_model(model_dir: Path):
    from tensorflow.core.protobuf import saved_model_pb2

    saved_model = saved_model_pb2.SavedModel()
    saved_model.ParseFromString((model_dir / "saved_model.pb").read_bytes())
    if len(saved_model.meta_graphs) != 1:
        raise RuntimeError(f"expected one MetaGraph, found {len(saved_model.meta_graphs)}")
    return saved_model


def embedded_stablehlo(saved_model) -> tuple[bytes, list[dict[str, Any]]]:
    occurrences = []
    payloads: dict[str, bytes] = {}
    meta_graph = saved_model.meta_graphs[0]
    for function in meta_graph.graph_def.library.function:
        for node in function.node_def:
            if node.op != "XlaCallModule":
                continue
            payload = bytes(node.attr["module"].s)
            identity = {
                "function": function.signature.name,
                "node": node.name,
                "version": int(node.attr["version"].i),
                "bytes": len(payload),
                "sha256": sha256_bytes(payload),
            }
            occurrences.append(identity)
            payloads[identity["sha256"]] = payload
    expected = EXPECTED_IDENTITIES["stablehlo_module"]
    if set(payloads) != {expected["sha256"]}:
        raise RuntimeError(f"unexpected embedded StableHLO identities: {sorted(payloads)}")
    payload = payloads[expected["sha256"]]
    if len(payload) != expected["bytes"]:
        raise RuntimeError(f"unexpected StableHLO byte count: {len(payload)}")
    return payload, occurrences


def _register_mlir():
    from jaxlib.mlir._mlir_libs import get_dialect_registry
    import jaxlib.mlir._mlir_libs._jax_mlir_ext as extension
    from jaxlib.mlir import ir
    from jaxlib.mlir.dialects import stablehlo

    extension.register_dialects(get_dialect_registry())
    context = ir.Context()
    stablehlo.register_dialect(context)
    return context


def _call_name(operation) -> str | None:
    from jaxlib.mlir.dialects import func

    return str(operation.callee).removeprefix("@") if isinstance(operation, func.CallOp) else None


def _owner_operation(value):
    owner = value.owner
    return owner.operation if hasattr(owner, "operation") else owner


def _opview(operation):
    return operation.opview


def _dependencies(values: Iterable[Any]) -> set[Any]:
    from jaxlib.mlir import ir

    keep = set()
    stack = list(values)
    while stack:
        value = stack.pop()
        if not isinstance(value, ir.OpResult):
            continue
        operation = _owner_operation(value)
        if operation in keep:
            continue
        keep.add(operation)
        stack.extend(operation.operands)
    return keep


def _function_map(module) -> dict[str, Any]:
    from jaxlib.mlir.dialects import func

    return {
        str(_opview(operation).name).strip('"'): _opview(operation)
        for operation in module.body.operations
        if isinstance(_opview(operation), func.FuncOp)
    }


def _called_functions(function) -> set[str]:
    from jaxlib.mlir.dialects import func

    names = set()
    for block in function.body.blocks:
        for operation in block.operations:
            view = _opview(operation)
            if isinstance(view, func.CallOp):
                names.add(_call_name(view))
    return names


def _reachable_functions(module, root: str = "main") -> set[str]:
    functions = _function_map(module)
    reachable = {root}
    stack = [root]
    while stack:
        name = stack.pop()
        for callee in _called_functions(functions[name]):
            if callee not in functions:
                raise RuntimeError(f"unresolved call target: {callee}")
            if callee not in reachable:
                reachable.add(callee)
                stack.append(callee)
    return reachable


def _helper_text(functions: dict[str, Any], name: str) -> str:
    return str(functions[name])


def _assert_scalar_constant(value, literal: str, label: str) -> None:
    if literal not in str(_owner_operation(value)):
        raise RuntimeError(f"{label} constant changed; expected StableHLO literal {literal}")


def trace_h1_authority(module) -> dict[str, Any]:
    """Mechanically trace H1 from the canonical StableHLO return graph."""
    from jaxlib.mlir.dialects import func

    functions = _function_map(module)
    main = functions["main"]
    block = main.body.blocks[0]
    original_args = list(block.arguments)
    if len(original_args) != 807:
        raise RuntimeError(f"expected 807 canonical main args, found {len(original_args)}")
    if str(original_args[-1].type) != "tensor<1x3x224x281xf32>":
        raise RuntimeError(f"unexpected canonical input type: {original_args[-1].type}")

    old_return = _opview(list(block.operations)[-1])
    if not isinstance(old_return, func.ReturnOp) or len(old_return.operands) != 2:
        raise RuntimeError("canonical main terminator is not the expected two-value return")
    full_scores, embedding = old_return.operands
    if str(full_scores.type) != "tensor<1x11560xf32>":
        raise RuntimeError(f"unexpected full score type: {full_scores.type}")
    if str(embedding.type) != "tensor<1x1280xf32>":
        raise RuntimeError(f"unexpected embedding type: {embedding.type}")

    final_sigmoid = _opview(_owner_operation(full_scores))
    if _call_name(final_sigmoid) != "_aten_sigmoid_83dd7e2f":
        raise RuntimeError(f"unexpected final score producer: {_call_name(final_sigmoid)}")
    full_fusion = final_sigmoid.operands[0]
    fusion_add_2 = _opview(_owner_operation(full_fusion))
    fusion_add_1 = _opview(_owner_operation(fusion_add_2.operands[0]))
    h1_weighted = fusion_add_1.operands[0]
    h2_weighted = fusion_add_1.operands[1]
    h3_weighted = fusion_add_2.operands[1]
    h1_weight_op = _opview(_owner_operation(h1_weighted))
    h2_weight_op = _opview(_owner_operation(h2_weighted))
    h3_weight_op = _opview(_owner_operation(h3_weighted))
    h1_logits = h1_weight_op.operands[0]

    for op, literal, label in [
        (h1_weight_op, "4.000000e-01", "H1 fusion weight"),
        (h2_weight_op, "3.500000e-01", "H2 fusion weight"),
        (h3_weight_op, "2.500000e-01", "H3 fusion weight"),
    ]:
        if "stablehlo.multiply" not in _helper_text(functions, _call_name(op)):
            raise RuntimeError(f"{label} edge is not multiplication")
        _assert_scalar_constant(op.operands[1], literal, label)
    for op, label in [(fusion_add_1, "first fusion join"), (fusion_add_2, "second fusion join")]:
        if "stablehlo.add" not in _helper_text(functions, _call_name(op)):
            raise RuntimeError(f"{label} is not addition")

    h1_divide = _opview(_owner_operation(h1_logits))
    if "stablehlo.divide" not in _helper_text(functions, _call_name(h1_divide)):
        raise RuntimeError("H1 branch does not end in averaging division")
    _assert_scalar_constant(h1_divide.operands[1], "dense<5>", "H1 average divisor")

    h1_ops = _dependencies([h1_logits])
    h1_calls = [_opview(operation) for operation in h1_ops if isinstance(_opview(operation), func.CallOp)]
    live_mm = sorted(
        _call_name(operation)
        for operation in h1_calls
        if _call_name(operation).startswith("_aten_mm_")
    )
    if len(live_mm) != 5:
        raise RuntimeError(f"expected five live H1 classifier passes, found {len(live_mm)}")
    weight_uses = sum(
        1
        for use in original_args[451].uses
        if (use.owner.operation if hasattr(use.owner, "operation") else use.owner) in h1_ops
    )
    bias_uses = sum(
        1
        for use in original_args[452].uses
        if (use.owner.operation if hasattr(use.owner, "operation") else use.owner) in h1_ops
    )
    if (weight_uses, bias_uses) != (5, 5):
        raise RuntimeError(
            f"expected five H1 classifier weight/bias uses, found {(weight_uses, bias_uses)}"
        )
    forbidden_random = sorted(
        name
        for operation in h1_ops
        for name in [operation.name]
        if "rng" in name.lower() or "random" in name.lower() or "dropout" in name.lower()
    )
    if forbidden_random:
        raise RuntimeError(f"unexpected stochastic operation in inference H1 slice: {forbidden_random}")

    feature = embedding
    gem_chain = []
    for _ in range(7):
        producer = _opview(_owner_operation(feature))
        gem_chain.append(_call_name(producer) or producer.operation.name)
        feature = producer.operands[0]
    shared_feature = feature
    if str(shared_feature.type) != "tensor<1x7x9x1280xf32>":
        raise RuntimeError(f"unexpected shared feature type: {shared_feature.type}")
    expected_gem_chain = [
        "_aten_unsafe_view_a3dfae78",
        "_aten_pow_5f058cbf",
        "_aten_mean_abe74551",
        "_aten_pow_9f8f1ed6",
        "_aten_clamp_8ecb06ea",
        "_aten_clone_568b95fc",
        "permute_65f95971",
    ]
    if gem_chain != expected_gem_chain:
        raise RuntimeError(f"unexpected GeM chain: {gem_chain}")
    if len(list(original_args[450].uses)) != 2:
        raise RuntimeError("learned GeM exponent arg450 does not have the expected two live uses")
    clamp_text = _helper_text(functions, "_aten_clamp_8ecb06ea")
    mean_text = _helper_text(functions, "_aten_mean_abe74551")
    if "9.9999999999999995E-7" not in clamp_text or "stablehlo.maximum" not in clamp_text:
        raise RuntimeError("GeM clamp is not maximum(x, float32(1e-6))")
    if "dimensions = [3, 2]" not in mean_text or "6.300000e+01" not in mean_text:
        raise RuntimeError("GeM mean does not reduce NCHW axes [3,2] with divisor 63")

    h2_ops = _dependencies([h2_weighted])
    h3_ops = _dependencies([h3_weighted])
    shared_owner = _owner_operation(shared_feature)
    if shared_owner not in h1_ops or shared_owner not in h2_ops or shared_owner not in h3_ops:
        raise RuntimeError("candidate feature is not the common H1/H2/H3 dependency")

    return {
        "main": main,
        "block": block,
        "old_return": old_return,
        "original_args": original_args,
        "final_sigmoid": final_sigmoid,
        "h1_logits": h1_logits,
        "embedding": embedding,
        "shared_feature": shared_feature,
        "proof": {
            "schema": "birdnet-clean-tensorflow-h1-contract",
            "version": 1,
            "state": "PROVEN",
            "authority": "official TensorFlow FP32 SavedModel embedded StableHLO",
            "canonical_main_argument_count": 807,
            "canonical_input_argument_index": 806,
            "canonical_input_type": str(original_args[806].type),
            "waveform_frontend_output": "NHWC float32 [1,224,281,3]",
            "stablehlo_input_after_transpose": "NCHW float32 [1,3,224,281]",
            "shared_feature": {
                "layout": "NHWC",
                "shape": SHARED_FEATURE_SHAPE,
                "common_dependency_of": ["H1", "H2", "H3"],
            },
            "gem": {
                "parameter_argument": 450,
                "parameter_shape": [1],
                "input_permute": "NHWC [1,7,9,1280] to NCHW [1,1280,7,9]",
                "clamp_epsilon": 1e-6,
                "spatial_axes_nchw": [3, 2],
                "spatial_element_count": 63,
                "order": "maximum -> power(p) -> mean -> reciprocal(p) -> power -> reshape",
                "chain_from_embedding_backwards": gem_chain,
            },
            "embedding": {
                "shape": EMBEDDING_SHAPE,
                "semantics": "exact H1 GeM output before classifier",
                "is_full_model_second_return_operand": True,
            },
            "classifier": {
                "weight_argument": 451,
                "bias_argument": 452,
                "weight_storage_shape": [11_560, 1280],
                "mathematical_weight_layout": "transpose to [1280,11560] before matrix multiply",
                "live_ordered_contributions": 5,
                "contribution_helpers": live_mm,
                "aggregation": "ordered sum from zero then divide by integer 5",
                "stochastic_operations_in_inference_slice": 0,
            },
            "full_predictor": {
                "fusion_weights": [0.4, 0.35, 0.25],
                "order": "sigmoid(0.4 * H1_logits + 0.35 * H2_logits + 0.25 * H3_logits)",
                "public_predictions_are_h1_scores": False,
            },
            "clean_h1_interface": {
                "production_outputs": {
                    "scores": {"shape": SCORES_SHAPE, "semantics": "sigmoid(unweighted H1 logits)"},
                    "embedding": {"shape": EMBEDDING_SHAPE, "semantics": "H1 GeM embedding"},
                },
                "validation_only_stages": ["shared_feature", "h1_logits"],
                "reason_logits_are_internal": "deployment output preserves score-plus-embedding form without carrying redundant logits",
            },
            "head_specific_argument_ranges": {
                "H1": [450, 451, 452],
                "H2_H3_only": list(range(453, 470)),
            },
        },
    }


def slice_h1_module(payload: bytes, mode: str) -> tuple[bytes, dict[str, Any]]:
    """Return a StableHLO slice using only shared inference and H1 state."""
    from jaxlib.mlir import ir
    from jaxlib.mlir.dialects import func, stablehlo

    if mode not in {"production", "probe"}:
        raise ValueError(f"unsupported slice mode: {mode}")
    context = _register_mlir()
    with context:
        module = stablehlo.deserialize_portable_artifact(context, payload)
        traced = trace_h1_authority(module)
        main = traced["main"]
        block = traced["block"]
        old_return = traced["old_return"]
        original_args = traced["original_args"]
        h1_logits = traced["h1_logits"]
        embedding = traced["embedding"]
        shared_feature = traced["shared_feature"]
        final_sigmoid = traced["final_sigmoid"]

        with ir.InsertionPoint(old_return):
            h1_sigmoid = func.CallOp(
                [h1_logits.type], final_sigmoid.callee, [h1_logits], loc=old_return.location
            )
            outputs = (
                [h1_sigmoid.result, embedding]
                if mode == "production"
                else [shared_feature, embedding, h1_logits, h1_sigmoid.result]
            )
            func.ReturnOp(outputs, loc=old_return.location)
        old_return.operation.erase()

        keep = _dependencies(outputs)
        keep.add(_owner_operation(h1_sigmoid.result))
        keep.add(list(block.operations)[-1])
        for operation in reversed(list(block.operations)):
            if operation not in keep:
                operation.erase()

        retained = [
            index for index, argument in enumerate(original_args) if len(list(argument.uses)) > 0
        ]
        if not retained or retained[-1] != 806:
            raise RuntimeError("canonical model input did not remain the final retained argument")
        leaked = sorted(set(range(453, 470)).intersection(retained))
        if leaked:
            raise RuntimeError(f"non-H1 head arguments remain live: {leaked}")
        for index in reversed(range(len(original_args))):
            if index not in retained:
                block.erase_argument(index)

        output_types = [value.type for value in outputs]
        input_types = [argument.type for argument in block.arguments]
        main.attributes["function_type"] = ir.TypeAttr.get(
            ir.FunctionType.get(input_types, output_types)
        )

        reachable = _reachable_functions(module)
        for operation in reversed(list(module.body.operations)):
            view = _opview(operation)
            if isinstance(view, func.FuncOp) and str(view.name).strip('"') not in reachable:
                operation.erase()
        if not module.operation.verify():
            raise RuntimeError("rewritten H1 module failed MLIR verification")
        artifact = stablehlo.serialize_portable_artifact_str(
            str(module), STABLEHLO_TARGET_VERSION
        )

    function_listing = "\n".join(sorted(reachable)).encode()
    removed = sorted(set(range(806)).difference(retained[:-1]))
    inference_dead = sorted(set(removed).difference(range(453, 470)))
    report = dict(traced["proof"])
    report.update({
        "slice_mode": mode,
        "source_module": {"bytes": len(payload), "sha256": sha256_bytes(payload)},
        "artifact": {"bytes": len(artifact), "sha256": sha256_bytes(artifact)},
        "stablehlo_target_version": STABLEHLO_TARGET_VERSION,
        "retained_main_argument_count": len(retained),
        "retained_variable_count": len(retained) - 1,
        "retained_original_variable_indices": retained[:-1],
        "removed_h2_h3_argument_indices": list(range(453, 470)),
        "removed_inference_dead_variable_indices": inference_dead,
        "retained_function_count": len(reachable),
        "retained_function_names_sha256": sha256_bytes(function_listing),
        "output_types": [str(value.type) for value in outputs],
        "live_full_predictor_call_present": False,
    })
    return artifact, report


def inspect_object_graph_binding(saved_model, loaded, module_report: dict[str, Any]) -> dict[str, Any]:
    """Prove StableHLO argument i is TensorFlow variable-list entry i."""
    object_graph = saved_model.meta_graphs[0].object_graph_def
    root = object_graph.nodes[0]
    root_children = {child.local_name: child.node_id for child in root.children}
    main_id = root_children["main"]
    main_children = {
        child.local_name: child.node_id for child in object_graph.nodes[main_id].children
    }
    static_id = main_children["_static_model"]
    static_children = {
        child.local_name: child.node_id for child in object_graph.nodes[static_id].children
    }
    variable_list_id = static_children["_variables"]
    variable_children = object_graph.nodes[variable_list_id].children
    if len(variable_children) != 806:
        raise RuntimeError(f"expected 806 object-graph variables, found {len(variable_children)}")
    expected_names = [str(index) for index in range(806)]
    actual_names = [child.local_name for child in variable_children]
    if actual_names != expected_names:
        raise RuntimeError("object-graph variable-list order is not canonical 0..805")

    runtime_variables = list(loaded.main._static_model._variables)
    if len(runtime_variables) != 806:
        raise RuntimeError(f"expected 806 runtime variables, found {len(runtime_variables)}")
    canonical_types = module_report["_canonical_argument_types"]
    mismatches = []
    for index, variable in enumerate(runtime_variables):
        tensor_type = _shape_to_mlir_tensor(variable.shape.as_list(), variable.dtype.name)
        if tensor_type != canonical_types[index]:
            mismatches.append({
                "index": index,
                "variable": tensor_type,
                "stablehlo_argument": canonical_types[index],
            })
    if mismatches:
        raise RuntimeError(f"TensorFlow-variable/StableHLO-argument type mismatches: {mismatches[:3]}")

    concrete = loaded.main._static_model.f[0].concrete_functions[0]
    if len(concrete.captured_inputs) != 806:
        raise RuntimeError(
            f"expected static function to capture 806 variables, found {len(concrete.captured_inputs)}"
        )
    for index, (capture, variable) in enumerate(zip(concrete.captured_inputs, runtime_variables)):
        if capture.dtype.name != "resource" or variable.handle.dtype.name != "resource":
            raise RuntimeError(f"capture {index} is not a resource handle")
        if capture is not variable.handle and capture.ref() != variable.handle.ref():
            raise RuntimeError(f"static function capture {index} is not variable-list entry {index}")

    return {
        "state": "PROVEN",
        "object_graph_root_main_node": main_id,
        "object_graph_static_model_node": static_id,
        "object_graph_variable_list_node": variable_list_id,
        "variable_list_entries": len(variable_children),
        "variable_list_local_names": "exact decimal sequence 0..805",
        "static_function_capture_count": len(concrete.captured_inputs),
        "capture_identity_check": "capture[i].ref() == _variables[i].handle.ref() for all 806",
        "shape_dtype_alignment_check": "StableHLO arg[i] == TensorFlow _variables[i] for all 806",
        "conclusion": "StableHLO arguments 450, 451 and 452 mechanically bind to TensorFlow variables 450, 451 and 452",
    }


def _shape_to_mlir_tensor(shape: list[int], dtype: str) -> str:
    dtype_map = {"float32": "f32", "int64": "i64", "int32": "i32"}
    if dtype not in dtype_map:
        raise RuntimeError(f"unsupported dtype in binding proof: {dtype}")
    dimensions = "x".join(str(value) for value in shape)
    return f"tensor<{dimensions + 'x' if dimensions else ''}{dtype_map[dtype]}>"


def canonical_argument_types(payload: bytes) -> list[str]:
    from jaxlib.mlir.dialects import stablehlo

    context = _register_mlir()
    with context:
        module = stablehlo.deserialize_portable_artifact(context, payload)
        main = _function_map(module)["main"]
        return [str(argument.type) for argument in main.body.blocks[0].arguments]


def inspect_h1_payload(payload: bytes) -> tuple[dict[str, Any], list[str]]:
    """Return serializable H1 proof and canonical main argument types."""
    from jaxlib.mlir.dialects import stablehlo

    context = _register_mlir()
    with context:
        module = stablehlo.deserialize_portable_artifact(context, payload)
        traced = trace_h1_authority(module)
        argument_types = [
            str(argument.type) for argument in traced["main"].body.blocks[0].arguments
        ]
        proof = dict(traced["proof"])
    return proof, argument_types


def make_clean_h1(source, production_module: bytes, retained_indices: list[int]):
    """Create a fresh TensorFlow module with copied values and no full-model parent."""
    tf = configure_tensorflow()
    from tensorflow.compiler.tf2xla.python import xla

    source_variables = list(source.main._static_model._variables)

    class CleanH1(tf.Module):
        def __init__(self):
            super().__init__(name="birdnet_v3_h1")
            self.frontend = source.pre
            self.h1_variables = [
                tf.Variable(
                    source_variables[index].read_value(),
                    trainable=False,
                    name=f"official_variable_{index:03d}",
                )
                for index in retained_indices
            ]
            self._production_module = production_module
            self.serve = tf.function(
                self._serve,
                input_signature=[
                    tf.TensorSpec(CANONICAL_INPUT_SHAPE, tf.float32, name="waveform")
                ],
                autograph=False,
            )

        def _serve(self, waveform):
            frontend = self.frontend.serve(waveform)
            model_input = tf.transpose(frontend, [0, 3, 1, 2])
            scores, embedding = xla.call_module(
                [variable.read_value() for variable in self.h1_variables] + [model_input],
                version=5,
                module=self._production_module,
                Tout=[tf.float32, tf.float32],
                Sout=[SCORES_SHAPE, EMBEDDING_SHAPE],
            )
            return {"scores": scores, "embedding": embedding}

    return CleanH1()


def run_xla_module(module: bytes, variables: list[Any], model_input, mode: str):
    tf = configure_tensorflow()
    from tensorflow.compiler.tf2xla.python import xla

    if mode == "production":
        tout = [tf.float32, tf.float32]
        sout = [SCORES_SHAPE, EMBEDDING_SHAPE]
        names = ["scores", "embedding"]
    elif mode == "probe":
        tout = [tf.float32] * 4
        sout = [SHARED_FEATURE_SHAPE, EMBEDDING_SHAPE, SCORES_SHAPE, SCORES_SHAPE]
        names = ["shared_feature", "embedding", "h1_logits", "scores"]
    else:
        raise ValueError(mode)
    values = xla.call_module(
        [variable.read_value() for variable in variables] + [model_input],
        version=5,
        module=module,
        Tout=tout,
        Sout=sout,
    )
    return dict(zip(names, values))


def prepare_frontend(source, waveform):
    tf = configure_tensorflow()
    frontend = source.pre.serve(tf.convert_to_tensor(waveform, dtype=tf.float32))
    if isinstance(frontend, dict):
        if len(frontend) != 1:
            raise RuntimeError(f"unexpected frontend mapping: {frontend.keys()}")
        frontend = next(iter(frontend.values()))
    if isinstance(frontend, (tuple, list)):
        if len(frontend) != 1:
            raise RuntimeError(f"unexpected frontend sequence length: {len(frontend)}")
        frontend = frontend[0]
    if frontend.shape.as_list() != [1, 224, 281, 3]:
        raise RuntimeError(f"unexpected frontend output shape: {frontend.shape}")
    return tf.transpose(frontend, [0, 3, 1, 2])


def generated_fixtures(canonical_path: Path, silence_path: Path) -> dict[str, np.ndarray]:
    canonical = np.load(canonical_path).astype(np.float32, copy=False)
    silence = np.load(silence_path).astype(np.float32, copy=False)
    if list(canonical.shape) != CANONICAL_INPUT_SHAPE or list(silence.shape) != CANONICAL_INPUT_SHAPE:
        raise RuntimeError("retained M1 fixture shapes changed")
    samples = CANONICAL_INPUT_SHAPE[1]
    index = np.arange(samples, dtype=np.float64)
    low = (1e-4 * np.sin(2.0 * np.pi * index * 997.0 / 32_000.0)).astype(np.float32)[None, :]
    high = (
        0.72 * np.sin(2.0 * np.pi * index * 431.0 / 32_000.0)
        + 0.23 * np.sin(2.0 * np.pi * index * 2801.0 / 32_000.0)
    )
    high = np.clip(high, -0.99, 0.99).astype(np.float32)[None, :]
    impulse = np.zeros(CANONICAL_INPUT_SHAPE, dtype=np.float32)
    impulse[0, [0, 1, 31_999, 32_000, 64_000, 95_999]] = [0.75, -0.5, 1.0, -1.0, 0.25, -0.25]
    return {
        "canonical": canonical,
        "silence": silence,
        "low_amplitude_sine": low,
        "high_valid_two_tone": high,
        "structured_impulse": impulse,
    }


def array_identity(value: Any) -> dict[str, Any]:
    array = np.asarray(value)
    finite = np.isfinite(array)
    result: dict[str, Any] = {
        "shape": list(array.shape),
        "dtype": str(array.dtype),
        "elements": int(array.size),
        "raw_sha256": array_sha256(array),
        "finite": bool(finite.all()),
    }
    if array.size and finite.any():
        selected = array[finite]
        result.update({
            "min": float(selected.min()),
            "max": float(selected.max()),
            "mean": float(selected.mean(dtype=np.float64)),
        })
    return result


def comparison_metrics(left: Any, right: Any) -> dict[str, Any]:
    a = np.asarray(left)
    b = np.asarray(right)
    if a.shape != b.shape or a.dtype != b.dtype:
        raise RuntimeError(
            f"comparison contract mismatch: {a.shape}/{a.dtype} versus {b.shape}/{b.dtype}"
        )
    finite = np.isfinite(a) & np.isfinite(b)
    exact_mask = a == b
    raw_equal = np.ascontiguousarray(a).tobytes() == np.ascontiguousarray(b).tobytes()
    result: dict[str, Any] = {
        "shape": list(a.shape),
        "dtype": str(a.dtype),
        "elements": int(a.size),
        "finite_pair_count": int(finite.sum()),
        "exact_equal_count": int(exact_mask.sum()),
        "raw_byte_equal": raw_equal,
        "nan_mask_equal": bool(np.array_equal(np.isnan(a), np.isnan(b))),
        "positive_infinity_mask_equal": bool(
            np.array_equal(np.isposinf(a), np.isposinf(b))
        ),
        "negative_infinity_mask_equal": bool(
            np.array_equal(np.isneginf(a), np.isneginf(b))
        ),
    }
    if finite.any():
        delta = np.abs(a[finite].astype(np.float64) - b[finite].astype(np.float64))
        result.update({
            "max_absolute_error": float(delta.max(initial=0.0)),
            "mean_absolute_error": float(delta.mean()),
            "rmse": float(np.sqrt(np.mean(delta * delta))),
        })
        denominator = np.maximum(
            np.maximum(np.abs(a[finite].astype(np.float64)), np.abs(b[finite].astype(np.float64))),
            1e-12,
        )
        result["max_symmetric_relative_error_floor_1e_12"] = float((delta / denominator).max(initial=0.0))
    return result


def artifact_xla_modules(model_dir: Path) -> list[dict[str, Any]]:
    saved_model = parse_saved_model(model_dir)
    rows = []
    for function in saved_model.meta_graphs[0].graph_def.library.function:
        for node in function.node_def:
            if node.op == "XlaCallModule":
                payload = bytes(node.attr["module"].s)
                rows.append({
                    "function": function.signature.name,
                    "node": node.name,
                    "bytes": len(payload),
                    "sha256": sha256_bytes(payload),
                    "version": int(node.attr["version"].i),
                })
    return rows
