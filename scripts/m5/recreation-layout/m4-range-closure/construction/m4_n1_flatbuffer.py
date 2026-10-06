#!/usr/bin/env python3
"""Deterministic TFLite FlatBuffer helpers for the frozen N1 experiment."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

import flatbuffers
import numpy as np
from tensorflow.lite.python import schema_py_generated as schema_fb


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def unpack_model(payload: bytes) -> Any:
    root = schema_fb.Model.GetRootAsModel(bytearray(payload), 0)
    return schema_fb.ModelT.InitFromObj(root)


def pack_model(model: Any) -> bytes:
    builder = flatbuffers.Builder(0)
    root = model.Pack(builder)
    builder.Finish(root, file_identifier=b"TFL3")
    return bytes(builder.Output())


def operator_builtin_code(model: Any, operator: Any) -> int:
    code = model.operatorCodes[int(operator.opcodeIndex)]
    return int(code.builtinCode)


def operator_name(model: Any, operator: Any) -> str:
    code = operator_builtin_code(model, operator)
    for name in dir(schema_fb.BuiltinOperator):
        if name.startswith("_"):
            continue
        if getattr(schema_fb.BuiltinOperator, name) == code:
            return name
    return f"BUILTIN_{code}"


def operator_inventory(model: Any, subgraph_index: int = 0) -> dict[str, int]:
    counts: dict[str, int] = {}
    for operator in model.subgraphs[subgraph_index].operators:
        name = operator_name(model, operator)
        counts[name] = counts.get(name, 0) + 1
    return dict(sorted(counts.items()))


def tensor_buffer_bytes(model: Any, tensor: Any) -> bytes:
    data = model.buffers[int(tensor.buffer)].data
    if data is None:
        return b""
    return np.asarray(data, dtype=np.uint8).tobytes()


def tensor_constant(model: Any, tensor_index: int) -> np.ndarray:
    tensor = model.subgraphs[0].tensors[tensor_index]
    payload = tensor_buffer_bytes(model, tensor)
    dtypes = {
        int(schema_fb.TensorType.INT32): np.dtype("<i4"),
        int(schema_fb.TensorType.INT16): np.dtype("<i2"),
        int(schema_fb.TensorType.INT8): np.dtype("i1"),
        int(schema_fb.TensorType.FLOAT32): np.dtype("<f4"),
    }
    if int(tensor.type) not in dtypes:
        raise RuntimeError(f"unsupported constant tensor type {tensor.type}")
    return np.frombuffer(payload, dtype=dtypes[int(tensor.type)]).copy()


def one_scale_zero_point(tensor: Any) -> tuple[np.float32, int]:
    quantization = tensor.quantization
    if quantization is None or quantization.scale is None or quantization.zeroPoint is None:
        raise RuntimeError(f"tensor {tensor.name!r} is not quantized")
    scales = np.asarray(quantization.scale, dtype=np.float32)
    zero_points = np.asarray(quantization.zeroPoint, dtype=np.int64)
    if scales.size != 1 or zero_points.size != 1:
        raise RuntimeError(f"tensor {tensor.name!r} is not per-tensor quantized")
    return scales[0], int(zero_points[0])


def float32_le_hex(value: Any) -> str:
    return np.asarray([value], dtype="<f4").tobytes().hex()


def consumers(subgraph: Any) -> dict[int, list[int]]:
    result: dict[int, list[int]] = {}
    for operator_index, operator in enumerate(subgraph.operators):
        for raw in np.asarray(operator.inputs, dtype=np.int32):
            tensor_index = int(raw)
            if tensor_index >= 0:
                result.setdefault(tensor_index, []).append(operator_index)
    return result


def mean_opcode_index(model: Any) -> int:
    expected_builtin = int(schema_fb.BuiltinOperator.MEAN)
    for index, code in enumerate(model.operatorCodes):
        if int(code.builtinCode) == expected_builtin and int(code.version) == 2:
            if int(code.deprecatedBuiltinCode) != expected_builtin:
                raise RuntimeError("existing MEAN v2 has inconsistent deprecated code")
            return index
    code = schema_fb.OperatorCodeT()
    code.builtinCode = expected_builtin
    code.deprecatedBuiltinCode = expected_builtin
    code.version = 2
    code.customCode = None
    model.operatorCodes.append(code)
    return len(model.operatorCodes) - 1


def make_mean_operator(opcode_index: int, input_tensor: int, axes_tensor: int, output_tensor: int) -> Any:
    options = schema_fb.ReducerOptionsT()
    options.keepDims = False
    operator = schema_fb.OperatorT()
    operator.opcodeIndex = opcode_index
    operator.inputs = np.asarray([input_tensor, axes_tensor], dtype=np.int32)
    operator.outputs = np.asarray([output_tensor], dtype=np.int32)
    operator.builtinOptionsType = int(schema_fb.BuiltinOptions.ReducerOptions)
    operator.builtinOptions = options
    return operator


def assert_deterministic_pack(model: Any) -> bytes:
    first = pack_model(model)
    second = pack_model(unpack_model(first))
    if first != second:
        raise RuntimeError(
            f"FlatBuffer object-api pack is not a fixed point: {sha256_bytes(first)} != {sha256_bytes(second)}"
        )
    return first


def model_identity(payload: bytes, path: Path | None = None) -> dict[str, Any]:
    result = {"bytes": len(payload), "sha256": sha256_bytes(payload)}
    if path is not None:
        result["path"] = path.as_posix()
    return result
