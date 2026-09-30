# BirdNET V3 H1 Clean Reproduction and Live Inference on Alif U8 DK

**Living technical paper — TensorFlow foundation M1–M3 accepted; M4–M5 not
started**

## Abstract

This work establishes a clean, reproducible BirdNET V3 H1 foundation directly
from the official BirdNET+ V3.0 Preview 3.1 Global 11K TensorFlow FP32
SavedModel. M1 mechanically recovers the exact H1 computation from the released
TensorFlow object rather than inferring it from shapes, familiar architecture
names or another framework. M2 creates a TensorFlow-native backward slice with
673 exact official parameter/state tensors and validates it under prospectively
frozen structural and numerical criteria. M3 serializes that model as a
standard TensorFlow SavedModel, independently reloads it, reproduces the entire
four-file tree byte for byte in a second constructor process, and binds its
aggregate SHA-256 for future conversion. Across five frozen fixtures, all
official/in-memory/reloaded production comparisons are raw-byte equal.
PyTorch and ONNX do not participate. No TFLite conversion, quantization, Vela,
M55, firmware or hardware work is claimed here.

## 1. Scope and lineage

The accepted production lineage is:

```text
official TensorFlow FP32
        ↓
clean H1 TensorFlow FP32
        ↓
TensorFlow Lite deployment model
        ↓
Vela / M55 execution
        ↓
Alif U8 DK
```

This report reaches only the canonical serialized TensorFlow FP32 H1 boundary.
It does not retrain, remove classes, implement H2/H3, perform regional class
projection, create dynamic head packs, schedule multiple NPUs or alter model
semantics for accelerator convenience. The shared-feature contract remains
explicit so future work is not artificially prevented from separating a shared
backbone and regional heads.

## 2. Official authority

M1 uses Zenodo record `20703646`, DOI `10.5281/zenodo.20703646`.

| Object | Bytes | SHA-256 |
|---|---:|---|
| official TensorFlow FP32 Protobuf archive | 499,609,919 | `ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb` |
| extracted `saved_model.pb` | 3,870,764 | `4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e` |
| embedded StableHLO payload | 313,747 | `f8f5968213692c2a4b17e5b504736a601fc60b4b3203bab51bc1c5cc8b8c2dac` |
| official labels CSV | 809,172 | `8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0` |

The official labels contain 11,560 ordered data rows. The released full FP32
TFLite identity is retained only as corroborating release metadata; that file
was not acquired or used during this rework. The former FP16-pruned release is
also not an implementation or numerical-authority input.

## 3. Runtime and method

All acceptance execution uses CPython 3.12.14, TensorFlow 2.21.0 and NumPy
2.2.6. TensorFlow intra-op and inter-op thread counts are one. OpenMP, MKL,
OpenBLAS and NumExpr thread counts are one. `PYTHONHASHSEED=0`, deterministic
TensorFlow operations and disabled oneDNN are fixed; constant folding and the
TensorFlow meta optimizer are disabled. Exact packages and wheel hashes are
retained under `requirements/`.

The method follows four evidence rules:

1. authoritative identities and predecessor accepted state are verified first;
2. semantic boundaries are established mechanically from TensorFlow-native
   objects and dependency graphs;
3. fixtures, numerical limits and implementation identities are frozen before
   final results;
4. generated model payloads stay in ignored scratch while Git retains compact
   identities, validators and conclusions.

## 4. Mechanically established H1 contract

The public source signature accepts float32 `[1,dynamic_samples]` and emits
`predictions` `[1,11560]` plus `embeddings` `[1,1280]`. Those public output
names do not by themselves identify H1. M1 instead aligns all 806 TensorFlow
variables, concrete-function captures and StableHLO argument types, then traces
the StableHLO dependency graph.

The canonical project domain is a float32 waveform `[1,96000]`: three seconds
at 32 kHz. The frontend produces NHWC `[1,224,281,3]`, which is transposed to
NCHW `[1,3,224,281]` for the embedded module. The shared feature boundary is
NHWC `[1,7,9,1280]`.

H1 transposes the feature to NCHW, clamps it at exact float32 `1e-6`, raises it
to learned exponent `7.408084869384766`, averages spatial axes `[3,2]` with
divisor 63, and applies the reciprocal-exponent root to form embedding
`[1,1280]`. The classifier is stored `[11560,1280]`, transposed for
`[1280,11560]` multiplication, and applied in five live ordered contributions
that are summed and divided by five. This produces unweighted H1 logits.

The released public prediction is
`sigmoid(0.4*H1 + 0.35*H2 + 0.25*H3)`, not an H1 score. The clean H1 production
interface therefore applies the same sigmoid helper to unweighted H1 logits
and returns named H1 `scores` `[1,11560]` plus `embedding` `[1,1280]`. Shared
feature and H1 logits remain validation/construction boundaries rather than
public production outputs.

## 5. M1 result

M1 disposition is **ACCEPTED**. The object graph/capture/type alignment proves
the variable-to-StableHLO relationship rather than assuming it from index or
shape. It identifies 670 live shared tensors, the GeM exponent, H1 classifier
weight and H1 bias. H2/H3-specific arguments 453–469 and 116 inference-dead
source variables are outside the H1 production slice. Positional output width
matches all 11,560 exact official labels.

Adversarial checks cover variable-index assumptions, axes, layout, GeM
identification, five-contribution evaluation behavior, cross-head dependencies,
activation semantics and label order. No M1 item is `UNRESOLVED`.

## 6. M2 construction

M2 backward-slices the exact released StableHLO computation for H1 production
outputs and builds a fresh TensorFlow module. It copies only the 673 retained
official values; it does not retain the full predictor as a parent or hidden
call target. The production StableHLO payload is 249,046 bytes with SHA-256
`3cff5699feedb7cde9c877f88ecaefc9c73da44fb735875411f5fdd10f78ecda`.
The validation-only probe additionally exposes shared feature and H1 logits.

All 673 retained tensors match the official TensorFlow values exactly in
shape, dtype, raw SHA-256 and value. The production graph contains no H2/H3
live computation, no H2/H3 learned state and no full-predictor payload.

## 7. M2 numerical result

M2 disposition is **ACCEPTED**. Five prospectively frozen fixtures cover the
canonical waveform, silence, a low-amplitude sine, a high-valid-amplitude
two-tone waveform and a structured impulse.

For every fixture, official-source probe versus clean copied-value probe is
raw-byte equal at shared feature, embedding, H1 logits and H1 scores. Official
probe versus the clean production interface is also raw-byte equal for
embedding and scores. All tested outputs are finite and all nonfinite masks
agree.

Independent TensorFlow arithmetic observed the following worst fixture errors:

| Stage | Maximum absolute | Maximum mean absolute | Maximum RMSE |
|---|---:|---:|---:|
| embedding | `5.960464477539063e-08` | `7.247763278428465e-10` | `3.6437141072832567e-09` |
| H1 logits | `3.910064697265625e-05` | `6.998183405523069e-06` | `8.915962392917153e-06` |
| H1 scores | `7.12461769580841e-08` | `5.758345329976531e-10` | `2.313116143740927e-09` |

These are finite observations over the fixtures, not universal bounds. Every
result satisfies its precommitted criterion. The released fused public
prediction differs from H1-only scores, confirming that it was not silently
used as the H1 target. No M2 item is `UNRESOLVED`.

## 8. M3 serialization and determinism

M3 disposition is **ACCEPTED**. A fresh process serializes the M2 model as a
standard TensorFlow SavedModel. A second independent reconstruction run uses
the same deterministic constructor implementation in another fresh process
and emits the same complete tree byte for byte. This proves repeatable
execution; it does not claim two independently implemented constructors.

TensorFlow 2.21 initially produced identical graph/checkpoint bytes and
identical semantic fingerprint hashes but a random protobuf field 7,
`FingerprintDef.uuid`. Immediately after `tf.saved_model.save`, the accepted
constructor deterministically derives only that decimal UUID from the first 16
bytes of SHA-256 over sorted `name=value` lines for the five semantic
fingerprint fields and `version.producer`, then deterministically serializes
only `fingerprint.pb`. No graph computation, StableHLO program, retained
variable/value, public signature or label-order byte is modified. Both outputs
reload through the public TensorFlow SavedModel API. This controlled harness
fix changed no fixture, threshold or structural contract.

The canonical artifact is:

| Relative path | Bytes | SHA-256 |
|---|---:|---|
| `fingerprint.pb` | 97 | `ce0efa1ec1ac4312d389d8bf785494bb07c200c85a47e40040a34d91e0b21ec5` |
| `saved_model.pb` | 3,100,807 | `d9b844205a70cd9f59b000f3afed287e10d186c4fb7bd4806ef78a969cae82b6` |
| `variables/variables.data-00000-of-00001` | 140,627,678 | `9c32a8d788d2893d38dcdda32de4b261ee8e7ec04514500550700260fd40a99f` |
| `variables/variables.index` | 38,755 | `54ac43bab03edbe554075011c10bf6564c082a18e817eb800ddec0961a3105ae` |

The four files total 143,767,337 bytes. Their aggregate tree SHA-256 is
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.

## 9. M3 independent reload result

After both reconstruction runs exit, an independent process loads both SavedModels
through `tf.saved_model.load`. The public signature is `waveform` float32
`[1,96000]` to named `scores` float32 `[1,11560]` and `embedding` float32
`[1,1280]`.

The checkpoint has 674 entries: one object graph and exactly 673 tensors in
`h1_variables/0..672`. Every official/canonical/rebuild parameter value hash
matches. The only embedded module has the accepted pruned H1 hash; the full
predictor, H2/H3 live computation, H2/H3 stored learned state and dead learned
state are absent.

Across all five fixtures, official A versus in-memory B, official A versus
reloaded C, in-memory B versus C, and canonical C versus the independent
rebuild are raw-byte equal for every applicable production output. Observed
worst maximum absolute error, mean absolute error and RMSE are all `0.0`.
Two fresh validation processes emit byte-identical compact evidence. No M3
adversarial item is `UNRESOLVED`.

## 10. Dependency result

Static AST inspection of the M1–M3 implementation and runtime module inspection
find no `torch`, `torchvision`, `torchaudio`, `onnx` or `onnxruntime`. No
PyTorch/ONNX package is executed, no such model supplies weights, and neither
framework is a numerical or conversion authority.

## 11. Evidence retention

Git retains milestone definitions, completion reports, validators, exact
runtime/source/artifact identities, compact numerical matrices, accepted-state
records and evidence manifests. The 500 MB upstream archive, extracted source,
143 MB canonical SavedModel, independent rebuild, virtual environment and
intermediate StableHLO files remain under ignored scratch. No generated TFLite,
Vela, firmware or calibration payload is present.

The immutable future-M4 binding is
`records/m3/CANONICAL_M4_FP32_INPUT.json`; substitution is forbidden without a
new gate decision.

## 12. Limitations and next boundary

The fixtures establish engineering conformance only. They do not establish
biological or ecological accuracy, calibration quality, general behavior on
all waveforms, or correctness of any later quantized deployment model.

No production TFLite model exists in the accepted state. No quantization,
A16W8 work, calibration, Vela compilation, M55 implementation, firmware,
board programming, memory qualification or Alif U8 execution has begun. M4
entry is authorized by M3, but M4 has not started.

## 13. Conclusion

M1–M3 establish a mechanically traced, parameter-exact, numerically exact and
byte-reproducible TensorFlow FP32 H1 foundation from the official TensorFlow
release. The sole canonical serialized input for any future M4 has aggregate
tree SHA-256
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.
The project remains incomplete until later, separately gated work demonstrates
live H1 inference on the Alif U8 DK.
