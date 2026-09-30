# Accepted-state index

Only M1–M5 are project milestones. This index describes the active TensorFlow lineage; obsolete milestone reports remain in Git history.

| Milestone | Current state | Primary record |
|---|---|---|
| M1 | **ACCEPTED** | `records/m1/ACCEPTED_STATE.json` |
| M2 | **ACCEPTED** | `records/m2/ACCEPTED_STATE.json` |
| M3 | **ACCEPTED** | `records/m3/ACCEPTED_STATE.json` |
| M4 | **NOT STARTED** | Accepted M3 authorizes entry; no M4 work has begun |
| M5 | **NOT STARTED** | Requires accepted M4 |

The active production lineage is official TensorFlow FP32 → clean H1 TensorFlow FP32 → TensorFlow Lite → Vela/M55 → Alif U8 DK. PyTorch, ONNX and the official TFLite releases are not accepted M1–M3 implementation inputs.

M1 mechanically establishes the exact H1 contract. M2 validates the clean
in-memory TensorFlow model with 673 exact tensors and exact primary stage
outputs over five frozen fixtures. M3 binds a byte-reproducible four-file
SavedModel tree with SHA-256
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.
M4 entry is authorized but M4 has not started.
