# M3 — serialized production TensorFlow FP32 H1

State: **ACCEPTED**.

Gate position: accepted M2 authorizes M3. Artifact construction and independent reload validation are governed by the prospective criteria committed in `records/m3/PROSPECTIVE_CRITERIA.json`.

## Claim

M3 produces exactly one canonical standard TensorFlow SavedModel containing the accepted H1 waveform frontend, shared encoder, learned GeM, five-contribution classifier, H1 sigmoid scores and embedding. It is the sole FP32 input that a future M4 may convert.

The generated SavedModel remains in ignored `.m3-work/`; Git retains its deterministic constructor, complete file-tree identity, audit and numerical results.

## Serialization requirements

`scripts/construct_h1.py` runs in a fresh pinned CPython 3.12.14 / TensorFlow 2.21.0 process and writes:

- one `serving_default` signature;
- input `waveform`: float32 `[1,96000]`;
- output `scores`: float32 `[1,11560]`;
- output `embedding`: float32 `[1,1280]`;
- exactly 673 learned/state tensors;
- only the accepted pruned H1 StableHLO payload.

The full official predictor object is used only during construction. It must not remain as a parent, callable, graph payload, learned checkpoint state or accidental dependency.

## Independent reload and audit

After construction exits, `scripts/validate_serialized_h1.py` loads the artifact through normal public TensorFlow APIs in another process. It must verify signatures, dtypes, dimensions, checkpoint namespace, every parameter hash, labels, XlaCallModule payloads, absence of H2/H3 computation/state and absence of the full predictor.

Two separate reconstruction runs, each in a fresh process and using the same
deterministic constructor implementation, must emit byte-identical SavedModel
trees. Aggregate identity is SHA-256 over sorted rows:

```text
<file_sha256>  <file_bytes>  <relative_path>\n
```

## Numerical matrix

The same five M2 fixtures are frozen. The validator compares:

- A: official TensorFlow source validation probe;
- B: clean in-memory TensorFlow H1;
- C: independently reloaded canonical SavedModel.

A/B probe stages and A/B production outputs require raw-byte identity. A/C and B/C use the strict FP32 limits frozen before serialization results. Canonical C versus the independent byte-identical rebuild also requires raw-byte output identity.

## Closure

Acceptance requires a single `records/m3/CANONICAL_M4_FP32_INPUT.json` binding with immutable tree identity, no `UNRESOLVED` adversarial item and no PyTorch/ONNX participation. M3 makes no TFLite, quantization, Vela, M55, firmware or hardware claim. This task stops before M4.
