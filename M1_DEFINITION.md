# M1 — authoritative TensorFlow source and H1 contract

State: **ACCEPTED**.

## Claim

M1 identifies the exact official BirdNET+ V3.0 Preview 3.1 Global 11K TensorFlow FP32 SavedModel and mechanically establishes the H1 boundary contained in that object. TensorFlow is the sole model authority. Producer source, PyTorch, ONNX and the released TFLite files are not implementation inputs.

## Entry inputs

- Zenodo record `20703646`, DOI `10.5281/zenodo.20703646`.
- FP32 Protobuf archive SHA-256 `ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb`.
- extracted `saved_model.pb` SHA-256 `4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e`.
- embedded StableHLO SHA-256 `f8f5968213692c2a4b17e5b504736a601fc60b4b3203bab51bc1c5cc8b8c2dac`.
- official labels SHA-256 `8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0`.

## Required mechanical proof

The released TensorFlow object itself must prove:

1. waveform frontend and its connection to the StableHLO input;
2. the shared feature boundary and its use by H1/H2/H3;
3. the learned GeM argument, layout, axes, epsilon and exact operation order;
4. the H1 classifier parameter binding, storage/mathematical layout and five live evaluation contributions;
5. unweighted H1 logits versus weighted three-head fused logits;
6. final activation and clean H1 score semantics;
7. embedding semantics and exact widths;
8. shared, H1-specific, H2/H3-only and inference-dead state;
9. positional agreement with all 11,560 rows of the exact official labels file.

Shape or variable index alone is insufficient. The object graph, concrete-function captures, variable list, StableHLO argument types and StableHLO dependency graph must agree.

## Canonical contract

Input is float32 waveform `[1,96000]`, representing three seconds at 32 kHz. The retained frontend produces NHWC `[1,224,281,3]`, transposed to NCHW `[1,3,224,281]` for the embedded module.

The shared encoder boundary is NHWC float32 `[1,7,9,1280]`. H1 computes:

```text
shared feature
  -> NHWC-to-NCHW
  -> maximum(x, float32(1e-6))
  -> power(p)
  -> mean over NCHW axes [3,2], divisor 63
  -> power(1/p)
  -> embedding [1,1280]
  -> five ordered applications of the same [11560,1280] classifier and bias
  -> ordered sum / 5
  -> H1 logits [1,11560]
  -> sigmoid
  -> H1 scores [1,11560]
```

The production interface is `scores` and `embedding`; shared feature and logits remain mechanically addressable validation/construction boundaries. The released public `predictions` are not H1 scores: they are `sigmoid(0.4*H1 + 0.35*H2 + 0.25*H3)`.

## Acceptance gate

M1 is accepted only if `scripts/inspect_source.py` fails closed on any identity or structural mismatch, every adversarial issue has a `PROVEN` or `VALIDATED` disposition, and `records/m1/ACCEPTED_STATE.json` authorizes M2. Otherwise M1 is blocked and M2 must not start.
