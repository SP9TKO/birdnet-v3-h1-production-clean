# Decision log

## D-001 — Milestone namespace is fixed

State: **ACCEPTED**.

The project has exactly five reviewer-facing milestones: **M1, M2, M3, M4 and M5**. Iterations and diagnostics stay on working/history branches and are consolidated into the owning milestone before merge.

## D-002 — Accepted main is cumulative and compact

State: **ACCEPTED**.

Only accepted cumulative state belongs on `main`. Failed candidates, intermediate conversion artifacts, large generated files and diagnostic branches are not merged into accepted main.

## D-003 — Official full predictor is canonical

State: **ACCEPTED**.

The official full 11K FP32 predictor and exact official labels are the model-lineage authority. Clean derivatives must be traceable to those bytes.

## D-004 — M2 is the clean TensorFlow FP32 H1 reproduction

State: **ACCEPTED**.

M2 must slice or retain the official TensorFlow computation directly, copy only exact live official values and validate the in-memory H1 model under prospectively frozen criteria. The previous FP16-pruned characterization remains in Git history only.

## D-005 — M3 binds one serialized TensorFlow FP32 H1 artifact

State: **ACCEPTED**.

M3 serializes the accepted M2 model as a standard TensorFlow SavedModel, independently reloads it and writes the sole immutable `CANONICAL_M4_FP32_INPUT` binding.

## D-006 — TensorFlow FP32 remains the numerical authority

State: **ACCEPTED**.

No PyTorch/FP64 model is a current numerical authority. Float64 may be used only to compute comparison metrics over TensorFlow results; it does not define weights or a separate model.

## D-007 — Floating and integer conformance are separate

State: **ACCEPTED**.

Floating implementations are compared with the accepted official TensorFlow
FP32 authority using prospectively frozen precision-aware metrics. Once a
quantized integer arithmetic contract is sealed, implementation conformance is
deterministic and requires **0 mismatches / 0 LSB error**.

## D-008 — M4 and M5 are not split into extra project milestones

State: **ACCEPTED**.

M4 and M5 may contain internal engineering phases on working branches, but accepted `main` receives one final cumulative state per milestone.

## D-009 — Project scope is H1 through live Alif U8 DK inference

State: **ACCEPTED**.

The project display name is **BirdNET V3 H1 Clean Reproduction and Live Inference on Alif U8 DK**. Its completion condition is accepted live H1 inference on the Alif U8 Development Kit under M5.

## D-010 — V3 implementation has independent public lineage

State: **ACCEPTED**.

This V3 implementation is reconstructed from official public BirdNET V3 artifacts. It has no runtime or build dependency on the author’s earlier private BirdNET 2.4 implementation. Engineering methods are documented in this repository and do not import previous-project model artifacts or private repository state.

## D-011 — Reproduction must be scriptable and human-readable

State: **ACCEPTED**.

Every accepted artifact must be reproducible from authoritative inputs by retained source/configuration. The repository must also explain, in human-readable form, the input data contract, transformation, output semantics, accepted evidence and limitations.

## D-012 — Repository sprawl is a project failure mode

State: **ACCEPTED**.

Accepted main must not accumulate timestamped run forests or thousands of generated evidence files. One cumulative accepted state is preferred per milestone. A proposed milestone merge adding more than **100 tracked files** requires explicit user review before merge.

## D-013 — Scientific paper is a maintained project output

State: **ACCEPTED**.

`PAPER.md` is a living technical/scientific report covering the problem, method, implementation, results, limitations, conclusions and next steps. Pending milestones are identified as pending rather than filled with speculative results.

## D-014 — H1+H2+H3 reproduction is a separate future project

State: **ACCEPTED**.

After this H1 project completes live Alif U8 DK inference, a separate **BirdNET V3 H1+H2+H3 Clean Reproduction** project may reuse the validated methodology but starts again from authoritative full-model lineage.


## D-015 — TensorFlow FP32 is the frozen production foundation

State: **ACCEPTED**.

The official BirdNET+ V3.0 Preview 3.1 Global 11K TensorFlow FP32 SavedModel is the current model and numerical authority. The production lineage is TensorFlow FP32 → clean H1 TensorFlow FP32 → TensorFlow Lite → Vela/M55 → Alif U8 DK.

ONNX is not part of the production route. PyTorch is not a runtime, numerical-authority or conversion dependency. Legacy PyTorch/ONNX material may remain only as historical provenance or compact release evidence.

This decision supersedes any earlier interpretation of D-003, D-005 or D-006 that treated the PyTorch predictor, a PyTorch-derived clean H1 model, or the full-model FP64 oracle as current downstream authority.

## D-016 — Previous M2/M3 acceptance is retained only in Git history

State: **ACCEPTED**.

The former FP16-characterization M2 and PyTorch/FP64 M3 definitions are not
active milestones and do not authorize M4. The active M2 is clean TensorFlow
FP32 H1 reproduction; the active M3 is serialized/reloaded TensorFlow FP32 H1.
Both are now accepted. M4 entry is authorized, but M4 has not started.

## D-017 — Preserve future shared-feature deployment flexibility without expanding H1 scope

State: **ACCEPTED**.

M5 firmware for the present H1 project must use explicit model and tensor descriptors, a model-runner abstraction, distinguishable M55/NPU execution, explicit buffer ownership and an internal completion/event abstraction.

This is an interface-design constraint only. The current project does not implement regional label reduction, H2/H3 deployment, dynamic regional packs, HP→LP feature handoff, multi-NPU parallel heads, hot-swappable regional models or a zero-copy shared feature ABI.

## D-018 — Canonical SavedModel identity includes a deterministic fingerprint UUID

State: **ACCEPTED**.

TensorFlow 2.21 writes a random `FingerprintDef.uuid` even when every semantic
fingerprint field and all other SavedModel bytes are identical. M3 replaces
only that UUID with a deterministic content-derived decimal 128-bit value.
This preserves all semantic hashes and permits the prospective requirement that
two independent reconstruction runs using the same deterministic constructor
implementation emit byte-identical complete trees. The independence is between
executions and fresh processes, not between separately implemented constructors.


## D-019 — Pre-M5 hardware development is functional-first

State: **ACCEPTED**.

A development-only Pre-M5 hardware PoC may proceed while formal M4/M5 qualification remains incomplete or blocked. The development sequence is:

1. functional end-to-end PoC;
2. real-acoustic characterization;
3. optimization of measured bottlenecks;
4. optional architecture/regional-head optimization;
5. formal numerical qualification of the selected deployment stack.

The PoC characterizes internal numerical differences before deciding whether they matter. Exact internal equality is not made a universal PoC prerequisite unless a specific PoC experiment requires it. This decision does not weaken any formal M4/M5 acceptance rule.

Retraining or weight fine-tuning is not an initial hardware-remediation mechanism. It requires separate explicit authorization after implementation behavior and biological impact are characterized.

## D-020 — Deployment numerical identity includes the runtime/toolchain stack

State: **ACCEPTED**.

Serialized TFLite model bytes alone are not treated as a complete numerical deployment identity. Host/board conformance records must bind the relevant TFLite/LiteRT runtime version and reference/delegate mode, thread configuration, Vela version/configuration, SDK/driver, compiler and firmware identity.

When an internal host/board difference appears during PoC work, the first diagnostic is a bounded runtime/toolchain identity comparison. Per-operator or recursive stage localization is deferred unless cheap checks fail and the difference materially propagates, or a formal gate requires it.

## D-021 — Shared backbone feature must support optional H2/H3 consumers

State: **ACCEPTED**.

The `[1,7,9,1280]` shared backbone feature is the canonical firmware fan-out boundary.

The architecture must permit:

```text
H1_ONLY
H23_ONLY
H1_H23_FULL
```

H1 consumes the feature through GeM and the H1 classifier. Optional H2/H3 consume the spatial feature directly through a separately replaceable H23 module; they must not be forced through GeM or the H1 embedding.

This decision supersedes only a narrow implementation-scope interpretation of D-014 and D-017. Formal clean reproduction/acceptance of H2/H3 remains outside the present H1 milestone scope unless explicitly authorized. Development-only H23 integration may be performed later without redefining the current formal H1 acceptance target.
