# Accepted-state index

## Historical acceptance and clean public candidate

Historical M5 remains **ACCEPTED** at its original uploaded-waveform H1 scope. Its historical Matter source binding remains `MATTER_BINDING_UNRESOLVED_STOP`: the accepted workspace discovered Matter Kconfig inputs, but its exact source revision could not be bound.

The clean public candidate removes Matter prospectively through [the flat workspace manifest](firmware/h1/workspace/west.yml). It separately passed `H1_NO_MATTER_STATIC_AND_BUILD_EQUIVALENCE_PASS` and `H1_NO_MATTER_PHYSICAL_EQUIVALENCE_PASS`, reaching `NO_MATTER_PUBLIC_CANDIDATE_QUALIFIED`. This candidate has a new firmware identity; historical firmware byte equality is not claimed and historical M5 was not reaccepted.

The physical campaign covers only canonical and high_valid_two_tone with immediate repeats: four slots PASS, 12 downstream comparisons / 373,920 integer elements / 0 mismatch / 0 LSB, historical 8/8 boundaries byte-identical, repeat 8/8 boundaries byte-identical, and 8 U85 submissions / 8 completion IRQs. The historical five-fixture M5 campaign was not rerun. No biological-accuracy or arbitrary-board claim follows.

Use [the candidate authority](records/release/CANDIDATE_AUTHORITY.json), [static/build evidence](records/release/NO_MATTER_STATIC_BUILD.json), [physical evidence](records/release/NO_MATTER_PHYSICAL.json), [production setup](records/release/PRODUCTION_SETUP.json) and [candidate recreation](records/release/RECREATION.md). Wren remains a mandatory locally retained 384,000-byte build input; no public-only M5 or fresh historical executable-byte reconstruction is promised.

Only M1–M5 are project milestones. This index describes the active TensorFlow lineage; obsolete milestone reports remain in Git history.

| Milestone | Current state | Primary record |
|---|---|---|
| M1 | **ACCEPTED** | `records/m1/ACCEPTED_STATE.json` |
| M2 | **ACCEPTED** | `records/m2/ACCEPTED_STATE.json` |
| M3 | **ACCEPTED** | `records/m3/ACCEPTED_STATE.json` |
| M4 | **ACCEPTED** | `records/m4/ACCEPTED_STATE.json` |
| M5 | **ACCEPTED** | `records/m5/ACCEPTED_STATE.json` |

The active production lineage is official TensorFlow FP32 → clean H1 TensorFlow FP32 → TensorFlow Lite → Vela/M55 → Alif U8 DK. PyTorch, ONNX and the official TFLite releases are not accepted M1–M3 implementation inputs.

M1 mechanically establishes the exact H1 contract. M2 validates the clean
in-memory TensorFlow model with 673 exact tensors and exact primary stage
outputs over five frozen fixtures. M3 binds a byte-reproducible four-file
SavedModel tree with SHA-256
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.
M4 binds accepted range-covering V3. M5 closes exact integer arithmetic, exact production bridge/programmed identity and prospective physical V2 at uploaded-waveform H1 scope. V1 remains permanently failed; frontend/GeM floating and biological/performance limitations remain explicit in `records/m5/QUALIFIED_EVIDENCE.json`.

The original M5 documentation digest remains historical evidence in [DOCUMENTATION_IDENTITY.json](records/m5/DOCUMENTATION_IDENTITY.json). [DOCUMENTATION_SUPPLEMENT.json](records/release/DOCUMENTATION_SUPPLEMENT.json) binds the current candidate document hashes separately.
