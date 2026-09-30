# Project status

Date: 2026-09-30. Project: **BirdNET V3 H1 Clean Reproduction and Live Inference on Alif U8 DK**.

| Milestone | State | Current meaning |
|---|---|---|
| M1 | **ACCEPTED** | Official TensorFlow FP32 authority and H1 contract established |
| M2 | **ACCEPTED** | Clean in-memory TensorFlow FP32 H1 validated |
| M3 | **ACCEPTED** | Canonical serialized/reloaded TensorFlow FP32 H1 reconstructed reproducibly |
| M4 | **NOT ACCEPTED** | No formal deployment-model qualification is claimed |
| M5 | **NOT ACCEPTED** | No formal live-inference qualification is claimed |

## Accepted authority and H1 boundary

The authoritative source is the official BirdNET+ V3.0 Preview 3.1 Global 11K TensorFlow FP32 Protobuf SavedModel:

- archive SHA-256 `ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb`;
- `saved_model.pb` SHA-256 `4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e`;
- StableHLO SHA-256 `f8f5968213692c2a4b17e5b504736a601fc60b4b3203bab51bc1c5cc8b8c2dac`;
- labels SHA-256 `8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0`.

M1 establishes the H1 path from shared NHWC `[1,7,9,1280]` feature through learned GeM, five ordered classifier contributions, unweighted H1 logits, sigmoid and `[1,11560]` H1 scores. Released public predictions instead apply sigmoid after H1/H2/H3 fusion.

The canonical input is float32 `[1,96000]`, 32 kHz, three seconds. CPython 3.12.14, TensorFlow 2.21.0 and NumPy 2.2.6 are frozen. StableHLO tooling is build/inspection-only. PyTorch and ONNX do not participate in accepted M1–M3.

## Development boundary

The integrated H1 firmware and prior end-to-end development observations are retained as engineering context. The current firmware source has been consolidated under `firmware/h1/`; production and diagnostic builds require revalidation against the sanitized tree. The current frontend disposition is `CHARACTERIZATION_ONLY`, and promotion is `FORBIDDEN` until a justified complete-frontend numerical tolerance is frozen. No new physical qualification is claimed.

See [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) and [`records/development/DEVELOPMENT_STATUS.md`](records/development/DEVELOPMENT_STATUS.md).
