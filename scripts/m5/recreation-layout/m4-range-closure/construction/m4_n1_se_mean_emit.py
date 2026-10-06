#!/usr/bin/env python3
"""Future-only deterministic emission of the solved native SE-MEAN design.

The output remains a standard builtin-only TFLite model.  Exactly 30 canonical
INT16 SUM -> reciprocal-MUL pairs are replaced by builtin MEAN v2.  This file is
frozen now but MUST NOT be executed to emit production bytes in the freeze
session.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np
from tensorflow.lite.python import schema_py_generated as schema_fb

from m4_n1_flatbuffer import (
    assert_deterministic_pack,
    consumers,
    float32_le_hex,
    make_mean_operator,
    mean_opcode_index,
    model_identity,
    one_scale_zero_point,
    operator_builtin_code,
    operator_inventory,
    operator_name,
    sha256_bytes,
    tensor_buffer_bytes,
    tensor_constant,
    unpack_model,
)
from m4_n1_freeze_common import file_identity, write_json
from m4_n1_git import (
    head_commit,
    read_commit_file,
    require_implementation_freeze,
    require_paths_match_commit,
)


RANGE_STATE_PATH = (
    "records/m4/n1-classifier-range-freeze/PROSPECTIVE_FREEZE_STATE.json"
)
RANGE_IMPLEMENTATION_PATH = (
    "records/m4/n1-classifier-range-freeze/IMPLEMENTATION_DELTA_FREEZE.json"
)
CORRECTIVE_STATE_PATH = (
    "records/m4/n1-se-mean-topology-freeze/PROSPECTIVE_FREEZE_STATE.json"
)
CORRECTIVE_IMPLEMENTATION_PATH = (
    "records/m4/n1-se-mean-topology-freeze/EMITTER_IMPLEMENTATION_FREEZE.json"
)
EXPECTED_MEAN_COUNT = 30
EXPECTED_AXES = [1, 2]
CONTRACT_PATH = Path("records/m4/se-mean-implementation-study/CURRENT_SE_MEAN_CONTRACT.json")
CONTRACT_SHA256 = "6af68ee58ed3efcf3f723c5b4db393e01f94b4db2467834a3896c429ec0e3f38"
TOPOLOGY_PATH = Path("records/m4/n1-se-mean-topology-freeze/SE_MEAN_TOPOLOGY_INVENTORY.json")
TOPOLOGY_SHA256 = "48b6baac31b6beb60af51d2a1b281e5cac9092b663800093a9855a9ef8a244be"
SOURCE_BACKBONE_SHA256 = "98ede4b0c1a7120bb55440edb8562e4edf98637b43fee10ac76084134398597f"


def _shape(tensor: Any) -> list[int]:
    return [int(item) for item in np.asarray(tensor.shape, dtype=np.int64)]


def _single_output(operator: Any, label: str) -> int:
    outputs = [int(value) for value in np.asarray(operator.outputs, dtype=np.int32) if int(value) >= 0]
    if len(outputs) != 1:
        raise RuntimeError(f"{label} does not have one output")
    return outputs[0]


def _operator_inputs(operator: Any) -> list[int]:
    return [int(value) for value in np.asarray(operator.inputs, dtype=np.int32)]


def _consumer_edges(model: Any, tensor_index: int) -> list[dict[str, Any]]:
    edges = []
    for operator_index, operator in enumerate(model.subgraphs[0].operators):
        inputs = _operator_inputs(operator)
        for input_position, value in enumerate(inputs):
            if value == tensor_index:
                edges.append(
                    {
                        "operator": operator_index,
                        "operator_name": operator_name(model, operator),
                        "input_position": input_position,
                    }
                )
    return edges


def _match_pair(model: Any, sum_index: int, contract_row: dict[str, Any], uses: dict[int, list[int]]) -> dict[str, Any]:
    subgraph = model.subgraphs[0]
    sum_operator = subgraph.operators[sum_index]
    inputs = [int(value) for value in np.asarray(sum_operator.inputs, dtype=np.int32) if int(value) >= 0]
    if len(inputs) != 2:
        raise RuntimeError(f"SUM {sum_index} input arity changed")
    activation_index, axes_index = inputs
    axes = tensor_constant(model, axes_index).astype(np.int64).tolist()
    if axes != EXPECTED_AXES:
        raise RuntimeError(f"SUM {sum_index} axes {axes} != {EXPECTED_AXES}")
    sum_output = _single_output(sum_operator, f"SUM {sum_index}")
    sum_consumers = uses.get(sum_output, [])
    if len(sum_consumers) != 1:
        raise RuntimeError(f"SUM {sum_index} output has consumers {sum_consumers}")
    mul_index = sum_consumers[0]
    mul_operator = subgraph.operators[mul_index]
    if operator_builtin_code(model, mul_operator) != int(schema_fb.BuiltinOperator.MUL):
        raise RuntimeError(f"SUM {sum_index} sole consumer is {operator_name(model, mul_operator)}")
    mul_inputs = [int(value) for value in np.asarray(mul_operator.inputs, dtype=np.int32) if int(value) >= 0]
    if len(mul_inputs) != 2 or mul_inputs.count(sum_output) != 1:
        raise RuntimeError(f"MUL {mul_index} does not consume SUM {sum_index} exactly once")
    scalar_index = mul_inputs[0] if mul_inputs[1] == sum_output else mul_inputs[1]
    output_index = _single_output(mul_operator, f"MUL {mul_index}")

    activation = subgraph.tensors[activation_index]
    sum_tensor = subgraph.tensors[sum_output]
    scalar = subgraph.tensors[scalar_index]
    output = subgraph.tensors[output_index]
    if any(int(tensor.type) != int(schema_fb.TensorType.INT16) for tensor in (activation, sum_tensor, scalar, output)):
        raise RuntimeError(f"pair {sum_index}/{mul_index} is not entirely public INT16")
    input_scale, input_zp = one_scale_zero_point(activation)
    sum_scale, sum_zp = one_scale_zero_point(sum_tensor)
    scalar_scale, scalar_zp = one_scale_zero_point(scalar)
    output_scale, output_zp = one_scale_zero_point(output)
    scalar_codes = tensor_constant(model, scalar_index).astype(np.int64).tolist()
    actual = {
        "input_shape": _shape(activation),
        "output_shape": _shape(output),
        "N": int(np.prod(_shape(activation)[1:3], dtype=np.int64)),
        "input_scale_hex": float32_le_hex(input_scale),
        "sum_scale_hex": float32_le_hex(sum_scale),
        "scalar_scale_hex": float32_le_hex(scalar_scale),
        "scalar_codes": scalar_codes,
        "output_scale_hex": float32_le_hex(output_scale),
        "zero_points": [input_zp, sum_zp, scalar_zp, output_zp],
    }
    expected = {
        "input_shape": contract_row["input_shape"],
        "output_shape": contract_row["output_shape"],
        "N": contract_row["N"],
        "input_scale_hex": contract_row["input_scale_float32_le_hex"],
        "sum_scale_hex": float32_le_hex(np.float32(contract_row["current_physical_sum_scale"])),
        "scalar_scale_hex": float32_le_hex(np.float32(contract_row["current_scalar_scale"])),
        "scalar_codes": [contract_row["current_scalar_code"]],
        "output_scale_hex": contract_row["output_scale_float32_le_hex"],
        "zero_points": [0, 0, 0, 0],
    }
    if actual != expected:
        raise RuntimeError(
            f"canonical pair ordinal {contract_row['ordinal']} changed: actual={actual} expected={expected}"
        )
    scalar_raw = tensor_buffer_bytes(model, scalar)
    output_consumers = _consumer_edges(model, output_index)
    return {
        "ordinal": contract_row["ordinal"],
        "canonical_operation_id": contract_row["canonical_operation_id"],
        "sum_operator": sum_index,
        "mul_operator": mul_index,
        "input_tensor": activation_index,
        "axes_tensor": axes_index,
        "dead_sum_tensor": sum_output,
        "source_reciprocal_tensor": scalar_index,
        "output_tensor": output_index,
        "sum_to_mul_input_position": mul_inputs.index(sum_output),
        "reciprocal_to_mul_input_position": mul_inputs.index(scalar_index),
        "N": actual["N"],
        "input_shape": actual["input_shape"],
        "mean_axes": axes,
        "output_shape": actual["output_shape"],
        "input_scale_hex": actual["input_scale_hex"],
        "output_scale_hex": actual["output_scale_hex"],
        "reciprocal_raw_sha256": sha256_bytes(scalar_raw),
        "reciprocal_raw_hex": scalar_raw.hex(),
        "matched_output_consumer_edges": output_consumers,
    }



def _annotate_reciprocal_topology(model: Any, matches: list[dict[str, Any]]) -> None:
    target_by_mul = {int(item["mul_operator"]): item for item in matches}
    for item in matches:
        edges = _consumer_edges(model, int(item["source_reciprocal_tensor"]))
        target_operator = int(item["mul_operator"])
        other_se = [
            edge
            for edge in edges
            if edge["operator"] != target_operator and edge["operator"] in target_by_mul
        ]
        unrelated = [
            edge
            for edge in edges
            if edge["operator"] != target_operator and edge["operator"] not in target_by_mul
        ]
        same_target_extra = [
            edge
            for edge in edges
            if edge["operator"] == target_operator
            and edge["input_position"] != item["reciprocal_to_mul_input_position"]
        ]
        if len(edges) == 1 and not other_se and not unrelated and not same_target_extra:
            classification = "PRIVATE_SINGLE_CONSUMER"
        elif unrelated:
            classification = "SHARED_UNRELATED_USE"
        elif same_target_extra and not other_se:
            classification = "SHARED_EQUIVALENT_SE_USE"
        elif other_se and not same_target_extra:
            classification = "SHARED_OTHER_SE_USE"
        else:
            classification = "OTHER"
        item["reciprocal_consumer_edges"] = edges
        item["reciprocal_consumer_count"] = len(edges)
        item["reciprocal_topology_classification"] = classification


def discover_canonical_matches(
    model: Any, contract: dict[str, Any]
) -> list[dict[str, Any]]:
    if contract.get("state") != "COMPLETE":
        raise RuntimeError(
            f"sealed SE-MEAN contract state {contract.get('state')!r} != COMPLETE"
        )
    if contract.get("mean_count") != EXPECTED_MEAN_COUNT:
        raise RuntimeError("sealed SE-MEAN contract does not contain exactly 30 reductions")
    rows = sorted(contract["reductions"], key=lambda row: int(row["ordinal"]))
    if [int(row["ordinal"]) for row in rows] != list(
        range(1, EXPECTED_MEAN_COUNT + 1)
    ):
        raise RuntimeError("sealed SE-MEAN ordinals are incomplete")
    if len(model.subgraphs) != 1:
        raise RuntimeError("backbone must contain exactly one subgraph")
    subgraph = model.subgraphs[0]
    uses = consumers(subgraph)
    sum_indices = [
        index
        for index, operator in enumerate(subgraph.operators)
        if operator_builtin_code(model, operator) == int(schema_fb.BuiltinOperator.SUM)
    ]
    if len(sum_indices) != EXPECTED_MEAN_COUNT:
        raise RuntimeError(
            f"backbone has {len(sum_indices)} SUM operators; exactly "
            f"{EXPECTED_MEAN_COUNT} canonical pairs are required"
        )
    matches = [
        _match_pair(model, sum_index, row, uses)
        for sum_index, row in zip(sum_indices, rows, strict=True)
    ]
    if len(matches) != EXPECTED_MEAN_COUNT:
        raise RuntimeError(
            f"matched {len(matches)} canonical SE means, expected {EXPECTED_MEAN_COUNT}"
        )
    mul_indices = {int(item["mul_operator"]) for item in matches}
    if len(mul_indices) != EXPECTED_MEAN_COUNT:
        raise RuntimeError("canonical SE-MEAN MUL pairing is ambiguous")
    _annotate_reciprocal_topology(model, matches)
    return matches


def _match_projection(
    item: dict[str, Any], removed_mul_indices: list[int]
) -> dict[str, Any]:
    sum_operator = int(item["sum_operator"])
    expected_mean_operator = sum_operator - sum(
        index < sum_operator for index in removed_mul_indices
    )
    expected_output_consumers = [
        {
            **edge,
            "operator": int(edge["operator"])
            - sum(index < int(edge["operator"]) for index in removed_mul_indices),
        }
        for edge in item["matched_output_consumer_edges"]
    ]
    return {
        "se_index": int(item["ordinal"]),
        "canonical_operation_id": item["canonical_operation_id"],
        "source_sum_operator": sum_operator,
        "matched_mul_operator": int(item["mul_operator"]),
        "source_tensor": int(item["input_tensor"]),
        "axes_tensor": int(item["axes_tensor"]),
        "dead_sum_tensor": int(item["dead_sum_tensor"]),
        "source_reciprocal_tensor": int(item["source_reciprocal_tensor"]),
        "output_tensor": int(item["output_tensor"]),
        "sum_to_mul_input_position": int(item["sum_to_mul_input_position"]),
        "reciprocal_to_mul_input_position": int(
            item["reciprocal_to_mul_input_position"]
        ),
        "N": int(item["N"]),
        "mean_axes": item["mean_axes"],
        "input_shape": item["input_shape"],
        "output_shape": item["output_shape"],
        "input_scale_float32_le_hex": item["input_scale_hex"],
        "output_scale_float32_le_hex": item["output_scale_hex"],
        "reciprocal_raw_sha256": item["reciprocal_raw_sha256"],
        "reciprocal_raw_hex": item["reciprocal_raw_hex"],
        "reciprocal_topology_classification": item[
            "reciprocal_topology_classification"
        ],
        "reciprocal_consumer_count": int(item["reciprocal_consumer_count"]),
        "matched_output_consumer_edges": item["matched_output_consumer_edges"],
        "expected_emitted_output_consumer_edges": expected_output_consumers,
        "expected_emitted_mean_operator": expected_mean_operator,
    }


def _reciprocal_edge_contracts(
    matches: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    grouped: dict[int, list[dict[str, Any]]] = {}
    for item in matches:
        tensor_index = int(item["source_reciprocal_tensor"])
        grouped.setdefault(tensor_index, item["reciprocal_consumer_edges"])
    return [
        {
            "tensor": tensor_index,
            "all_consumer_edges": sorted(
                edges, key=lambda edge: (edge["operator"], edge["input_position"])
            ),
        }
        for tensor_index, edges in sorted(grouped.items())
    ]


def assert_frozen_topology(
    matches: list[dict[str, Any]], topology: dict[str, Any]
) -> None:
    if (
        topology.get("schema")
        != "birdnet-clean-m4-n1-se-mean-topology-inventory"
        or topology.get("state") != "COMPLETE"
        or topology.get("semantic_reduction_count") != EXPECTED_MEAN_COUNT
        or topology.get("unresolved") != []
    ):
        raise RuntimeError("prospective SE-MEAN topology inventory is not complete")
    removed = sorted(int(item["mul_operator"]) for item in matches)
    actual_mappings = [_match_projection(item, removed) for item in matches]
    if actual_mappings != topology.get("target_edge_mappings"):
        raise RuntimeError("authoritative SE-MEAN target edge mapping changed")
    actual_reciprocals = _reciprocal_edge_contracts(matches)
    expected_reciprocals = [
        {
            "tensor": int(item["tensor"]["tensor"]),
            "all_consumer_edges": [
                {
                    "operator": int(edge["operator"]),
                    "operator_name": edge["operator_name"],
                    "input_position": int(edge["input_position"]),
                }
                for edge in item["all_consumer_edges"]
            ],
        }
        for item in topology["reciprocal_objects"]
    ]
    if actual_reciprocals != expected_reciprocals:
        raise RuntimeError(
            f"authoritative reciprocal consumer edges changed: "
            f"{actual_reciprocals} != {expected_reciprocals}"
        )
    names = (
        "PRIVATE_SINGLE_CONSUMER",
        "SHARED_EQUIVALENT_SE_USE",
        "SHARED_OTHER_SE_USE",
        "SHARED_UNRELATED_USE",
        "OTHER",
    )
    actual_counts = {
        name: sum(item["reciprocal_topology_classification"] == name for item in matches)
        for name in names
    }
    if actual_counts != topology.get("per_reduction_classification_counts"):
        raise RuntimeError(
            f"reciprocal topology classification changed: {actual_counts}"
        )


def rewrite_matches_in_memory(
    model: Any, matches: list[dict[str, Any]]
) -> dict[str, Any]:
    subgraph = model.subgraphs[0]
    original_operators = list(subgraph.operators)
    tensor_objects = [id(tensor) for tensor in subgraph.tensors]
    buffer_objects = [id(buffer) for buffer in model.buffers]
    mul_indices = {int(item["mul_operator"]) for item in matches}
    match_by_sum = {int(item["sum_operator"]): item for item in matches}
    untouched = {
        index: operator
        for index, operator in enumerate(original_operators)
        if index not in mul_indices and index not in match_by_sum
    }
    mean_code = mean_opcode_index(model)
    new_operators = []
    for operator_index, operator in enumerate(original_operators):
        if operator_index in mul_indices:
            continue
        match = match_by_sum.get(operator_index)
        if match is None:
            new_operators.append(operator)
        else:
            new_operators.append(
                make_mean_operator(
                    mean_code,
                    int(match["input_tensor"]),
                    int(match["axes_tensor"]),
                    int(match["output_tensor"]),
                )
            )
    subgraph.operators = new_operators
    if tensor_objects != [id(tensor) for tensor in subgraph.tensors]:
        raise RuntimeError("SE-MEAN rewrite changed or duplicated tensor objects")
    if buffer_objects != [id(buffer) for buffer in model.buffers]:
        raise RuntimeError("SE-MEAN rewrite changed or duplicated buffer objects")
    surviving_ids = {id(operator) for operator in subgraph.operators}
    if any(id(operator) not in surviving_ids for operator in untouched.values()):
        raise RuntimeError("SE-MEAN rewrite changed a non-target operator")
    return {
        "strategy": "EDGE_LOCAL_PAIR_REPLACEMENT",
        "matched_pair_count": len(matches),
        "removed_mul_count": len(mul_indices),
        "emitted_mean_count": len(match_by_sum),
        "non_target_operator_objects_preserved": True,
        "tensor_objects_preserved": True,
        "buffer_objects_preserved": True,
        "constant_duplication_used": False,
        "consumer_ordering_used_for_matching": False,
    }



def emit_standard_means(
    payload: bytes, contract: dict[str, Any], topology: dict[str, Any]
) -> tuple[bytes, dict[str, Any]]:
    if sha256_bytes(payload) != SOURCE_BACKBONE_SHA256:
        raise RuntimeError(
            f"raw backbone identity {sha256_bytes(payload)} != {SOURCE_BACKBONE_SHA256}"
        )

    model = unpack_model(payload)
    before_inventory = operator_inventory(model)
    matches = discover_canonical_matches(model, contract)
    assert_frozen_topology(matches, topology)

    rewrite_proof = rewrite_matches_in_memory(model, matches)
    output = assert_deterministic_pack(model)
    rebuilt = unpack_model(output)
    after_inventory = operator_inventory(rebuilt)
    expected_inventory = dict(before_inventory)
    expected_inventory["SUM"] = expected_inventory.get("SUM", 0) - EXPECTED_MEAN_COUNT
    expected_inventory["MUL"] = expected_inventory.get("MUL", 0) - EXPECTED_MEAN_COUNT
    expected_inventory["MEAN"] = expected_inventory.get("MEAN", 0) + EXPECTED_MEAN_COUNT
    expected_inventory = {name: count for name, count in expected_inventory.items() if count}
    if dict(sorted(expected_inventory.items())) != topology.get(
        "expected_emitted_operator_inventory"
    ):
        raise RuntimeError("computed operator delta differs from frozen topology inventory")
    if after_inventory != dict(sorted(expected_inventory.items())):
        raise RuntimeError(f"emission operator delta changed: {after_inventory} != {expected_inventory}")
    forbidden = [name for name in after_inventory if name == "CUSTOM" or name.startswith("Flex")]
    if forbidden:
        raise RuntimeError(f"emission introduced forbidden operators: {forbidden}")
    return output, {
        "state": "VALIDATED",
        "design": "C_AS_ARITHMETIC + D_AS_EMISSION",
        "matched_count": len(matches),
        "matches": matches,
        "before_inventory": before_inventory,
        "after_inventory": after_inventory,
        "operator_delta": {"SUM": -30, "MUL": -30, "MEAN": 30},
        "rewrite_proof": rewrite_proof,
        "dead_tensor_policy": "former SUM outputs and the two shared reciprocal constants remain unreachable serializer baggage after all 30 matched MULs are removed; no tensor or buffer is deleted, duplicated, renumbered, or mutated",
        "public_arithmetic": "INT16 input -> builtin MEAN v2 with hidden/logical INT32 accumulation -> fixed-point Sx/(N*Sy) -> INT16 output",
        "custom_flex_or_select_tf_ops": False,
    }


def authorize_execution(flag: bool, freeze_commit: str) -> None:
    if not flag:
        raise RuntimeError("production emission requires --execute-frozen-experiment")
    head = head_commit()
    if head != freeze_commit:
        raise RuntimeError(f"HEAD {head} != authorized freeze commit {freeze_commit}")
    require_paths_match_commit(
        freeze_commit,
        (
            RANGE_STATE_PATH,
            RANGE_IMPLEMENTATION_PATH,
            CORRECTIVE_STATE_PATH,
            CORRECTIVE_IMPLEMENTATION_PATH,
            TOPOLOGY_PATH,
        ),
    )
    require_implementation_freeze(freeze_commit, CORRECTIVE_IMPLEMENTATION_PATH)
    range_state = json.loads(read_commit_file(freeze_commit, RANGE_STATE_PATH))
    if (
        range_state.get("disposition") != "FROZEN"
        or range_state.get("next_session_candidate_execution_authorized") is not True
    ):
        raise RuntimeError("inherited classifier-range freeze is not intact")
    state = json.loads(read_commit_file(freeze_commit, CORRECTIVE_STATE_PATH))
    if (
        state.get("disposition") != "FROZEN"
        or state.get("candidate_construction_authorized_in_this_session") is not False
        or state.get("next_session_candidate_execution_authorized") is not True
        or state.get("no_execution_overlay_required") is not True
        or state.get("corrected_emitter_must_be_used_directly") is not True
    ):
        raise RuntimeError(
            "freeze commit does not contain a closed corrected FROZEN pre-byte state"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--freeze-commit", required=True)
    parser.add_argument("--execute-frozen-experiment", action="store_true")
    args = parser.parse_args()
    authorize_execution(args.execute_frozen_experiment, args.freeze_commit)
    if args.output.exists() or args.report.exists():
        raise RuntimeError("refusing to overwrite production emission output")
    contract_identity = file_identity(CONTRACT_PATH)
    if contract_identity["sha256"] != CONTRACT_SHA256:
        raise RuntimeError("sealed SE-MEAN contract identity changed")
    contract = json.loads(CONTRACT_PATH.read_text())
    topology_identity = file_identity(TOPOLOGY_PATH)
    if topology_identity["sha256"] != TOPOLOGY_SHA256:
        raise RuntimeError("frozen SE-MEAN topology identity changed")
    topology = json.loads(TOPOLOGY_PATH.read_text())
    input_payload = args.input.read_bytes()
    output_payload, report = emit_standard_means(input_payload, contract, topology)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output_payload)
    report.update(
        {
            "input": model_identity(input_payload, args.input),
            "output": model_identity(output_payload, args.output),
            "contract": contract_identity,
            "topology": topology_identity,
            "implementation": [file_identity(Path(__file__)), file_identity(Path("scripts/m4_n1_flatbuffer.py"))],
            "freeze_commit": args.freeze_commit,
        }
    )
    write_json(args.report, report)


if __name__ == "__main__":
    main()
