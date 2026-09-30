# Reproducibility guide

Project: **BirdNET V3 H1 Clean Reproduction and Live Inference on Alif U8 DK**

This guide describes the accepted TensorFlow-only path through M3. No formal M4 or M5 acceptance is claimed; prior development observations are summarized separately.

## 1. Authoritative inputs

The sole model authority is Zenodo record `20703646`, DOI
`10.5281/zenodo.20703646`.

| Object | Bytes | SHA-256 | Published MD5 |
|---|---:|---|---|
| `BirdNET+_V3.0-preview3.1_Global_11K_FP32_Protobuf.zip` | 499,609,919 | `ead54e1c3c0cbf6032def4a5aa6e583b63263440f6975e2dbade7d925e775eeb` | `f0409a35ac3e1605cbe6190e0c81bba9` |
| extracted `saved_model.pb` | 3,870,764 | `4700fa91766af07e4923b549727afad9cd94310b01871ac17f717ae00d42631e` | — |
| embedded StableHLO payload | 313,747 | `f8f5968213692c2a4b17e5b504736a601fc60b4b3203bab51bc1c5cc8b8c2dac` | — |
| `BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv` | 809,172 | `8124b0ea2d187104c5e2cd95a0f937165647e20349c8fd34d4d5ef991821f8f0` | `21ccf9e984a955bfc2d5c52faab6a6ea` |

The labels file contains 11,560 data rows. The official full FP32 TFLite is
corroboration only and was not acquired or used in this rework. Neither the
official TFLite nor the former FP16-pruned artifact is a construction input.

Every acceptance script fails closed if the exact SavedModel, StableHLO or
label identity differs. The bootstrap below acquires the archive and labels
directly from the two Zenodo record file endpoints, verifies the archive and
labels before model use, extracts the verified archive, and then verifies the
extracted `saved_model.pb`.

## 2. Fresh-checkout bootstrap

The only host prerequisites are Git, Docker, `curl`, `unzip`, GNU
`sha256sum`, and `diff`. Starting without `.m1-work/`, `.m2-work/`, or
`.m3-work/`, run:

```bash
git clone https://github.com/SP9TKO/birdnet-v3-h1-production-clean.git
cd birdnet-v3-h1-production-clean
git switch master
scripts/bootstrap_m1_m3.sh
```

`scripts/bootstrap_m1_m3.sh` performs, in order:

1. authoritative archive and label acquisition from Zenodo record `20703646`;
2. SHA-256 verification of the archive and labels before extraction or use;
3. archive extraction into ignored `.m1-work/extracted/` and verification of
   the extracted `saved_model.pb` identity;
4. acquisition of the exact CPython 3.12.14 container by immutable image
   digest;
5. creation of ignored `.m2-work/venv` inside that container;
6. download of every pinned binary wheel into ignored `.m2-work/wheels`;
7. verification of every wheel against `requirements/WHEEL_SHA256SUMS.txt`
   before installation;
8. offline, no-dependency installation of
   `requirements/requirements-frozen.txt`, followed by `pip check` and an
   exact `pip freeze --all` comparison.

No pre-existing ignored resource is required. If a verified download already
exists, the script re-verifies it rather than trusting its path. Partial or
identity-mismatched extracted input fails closed. The virtual environment,
downloaded inputs, wheel cache, build trees, and generated SavedModels remain
ignored and are not committed.

## 3. Runtime

Acceptance used:

- Linux `amd64`, matching the frozen CPython wheels;
- CPython 3.12.14 in
  `python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156`;
- TensorFlow 2.21.0;
- NumPy 2.2.6;
- one TensorFlow intra-op and inter-op thread;
- one OpenMP, MKL, OpenBLAS and NumExpr thread;
- oneDNN disabled and deterministic TensorFlow operations enabled;
- TensorFlow constant folding disabled and the meta optimizer disabled.

The exact package set and wheel hashes are retained in `requirements/`. The
ignored environment used by the commands below is `.m2-work/venv`. `jaxlib`
and `scipy` are build/inspection tooling for StableHLO; they are not alternate
model authorities.

All acceptance invocations use:

```text
PYTHONHASHSEED=0
TF_DETERMINISTIC_OPS=1
TF_ENABLE_ONEDNN_OPTS=0
TF_NUM_INTRAOP_THREADS=1
TF_NUM_INTEROP_THREADS=1
OMP_NUM_THREADS=1
MKL_NUM_THREADS=1
OPENBLAS_NUM_THREADS=1
NUMEXPR_NUM_THREADS=1
CUDA_VISIBLE_DEVICES=
```

## 4. Accepted H1 contract

The canonical input is a three-second, 32 kHz mono waveform: float32
`[1,96000]`. The official frontend creates NHWC `[1,224,281,3]`, then the
StableHLO path receives NCHW `[1,3,224,281]`.

The mechanically proven path is:

```text
shared NHWC feature [1,7,9,1280]
  -> transpose to NCHW
  -> maximum(x, float32(1e-6))
  -> pow(p), p = 7.408084869384766
  -> mean across axes [3,2], divisor 63
  -> pow(1/p)
  -> embedding [1,1280]
  -> five ordered uses of the same H1 classifier and bias
  -> ordered sum / 5
  -> H1 logits [1,11560]
  -> sigmoid
  -> H1 scores [1,11560]
```

The stored classifier is `[11560,1280]` and is transposed for mathematical
`[1280,11560]` multiplication. The clean model's H1 scores are not the
released public predictions, which are
`sigmoid(0.4*H1 + 0.35*H2 + 0.25*H3)`. Output column `i` maps to official CSV
data row `i`.

## 5. Exact M1 and M2 verification

Run M1 source inspection after the bootstrap:

```bash
docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/inspect_source.py \
  --official .m1-work/official \
  --extracted .m1-work/extracted \
  --output records/m1

git diff --exit-code -- records/m1
```

`scripts/inspect_source.py` proves the M1 contract from the TensorFlow object
graph, concrete-function captures, variable list, StableHLO argument types and
StableHLO dependency graph. The final `git diff` requires the regenerated M1
evidence to equal the accepted compact records.

Construct and validate the clean in-memory M2 H1 in separate processes:

```bash
docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/construct_h1.py \
  --extracted .m1-work/extracted \
  --work .m2-work/build \
  --report .m2-work/CONSTRUCTION.json

docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/validate_h1.py \
  --extracted .m1-work/extracted \
  --labels .m1-work/official/BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv \
  --work .m2-work/build \
  --criteria records/m2/PROSPECTIVE_CRITERIA.json \
  --output records/m2

git diff --exit-code -- records/m2
```

`scripts/construct_h1.py` backward-slices the exact StableHLO module. The M2
production slice contains 673 exact official tensors: 670 shared tensors plus
the learned GeM exponent, H1 classifier weight and H1 bias. Its production
StableHLO SHA-256 is
`3cff5699feedb7cde9c877f88ecaefc9c73da44fb735875411f5fdd10f78ecda`.

`scripts/validate_h1.py` checks all 673 parameter identities, structure,
labels, five frozen fixtures, stagewise values and forbidden dependencies
against the unchanged `records/m2/PROSPECTIVE_CRITERIA.json`. The final
`git diff` requires the regenerated M2 evidence to equal the accepted records.

## 6. Exact M3 reconstruction

Run from the repository root. The container uses the stable internal path `/workspace` so generated paths do not depend on the host checkout location. The canonical constructor command is:

```bash
docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/construct_h1.py \
  --extracted .m1-work/extracted \
  --work .m3-work/canonical-build \
  --output-saved-model .m3-work/canonical-h1-savedmodel \
  --report .m3-work/CANONICAL_BUILD.json
```

Run the same deterministic constructor implementation independently in a
second fresh process, with separate work, output, and report paths:

```bash
docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/construct_h1.py \
  --extracted .m1-work/extracted \
  --work .m3-work/rebuild-build \
  --output-saved-model .m3-work/rebuild-h1-savedmodel \
  --report .m3-work/REBUILD.json
```

The two runs are independent executions of one implementation; they are not
independently implemented constructors.

TensorFlow 2.21 generates a different random decimal string in protobuf field
7, `FingerprintDef.uuid`, between otherwise byte-identical saves. A controlled
pre-normalization pair established that the five content fields
`saved_model_checksum`, `graph_def_program_hash`, `signature_def_hash`,
`saved_object_graph_hash`, and `checkpoint_hash`, plus `version.producer`, are
equal; `saved_model.pb` and both checkpoint files are also byte-identical.

Immediately after `tf.saved_model.save` returns, and before the constructor
computes the artifact tree identity, `normalize_fingerprint()`:

1. parses `fingerprint.pb` as `FingerprintDef`;
2. creates sorted `name=value` lines for those five fields and `producer`;
3. hashes the lines with SHA-256;
4. interprets the first 16 digest bytes as an unsigned big-endian integer;
5. stores its decimal representation in `FingerprintDef.uuid`; and
6. deterministically serializes only `fingerprint.pb`.

The canonical UUID is `52591591100515463165678265775315934915`. The code does
not open for writing `saved_model.pb`, either variables file, or the labels.
Consequently the normalization changes no graph computation, embedded
StableHLO program, retained variable or value, input/output signature, or label
order. Section 7 independently checks those facts and loads both canonicalized
artifacts through public `tf.saved_model.load`.

## 7. Exact M3 validation

After both reconstruction processes using the same constructor exit, run:

```bash
docker run --rm \
  --platform linux/amd64 \
  -v "$PWD:/workspace" \
  -w /workspace \
  -e PYTHONHASHSEED=0 \
  -e TF_DETERMINISTIC_OPS=1 \
  -e TF_ENABLE_ONEDNN_OPTS=0 \
  -e TF_NUM_INTRAOP_THREADS=1 \
  -e TF_NUM_INTEROP_THREADS=1 \
  -e OMP_NUM_THREADS=1 \
  -e MKL_NUM_THREADS=1 \
  -e OPENBLAS_NUM_THREADS=1 \
  -e NUMEXPR_NUM_THREADS=1 \
  -e CUDA_VISIBLE_DEVICES= \
  python:3.12.14-slim-bookworm@sha256:8cbe7fcd5df843c789eb26a3d3059859469441633d6e671111f31553e8dc7156 \
  .m2-work/venv/bin/python scripts/validate_serialized_h1.py \
  --extracted .m1-work/extracted \
  --labels .m1-work/official/BirdNET+_V3.0-preview3.1_Global_11K_Labels.csv \
  --artifact .m3-work/canonical-h1-savedmodel \
  --rebuild-artifact .m3-work/rebuild-h1-savedmodel \
  --build-report .m3-work/CANONICAL_BUILD.json \
  --rebuild-report .m3-work/REBUILD.json \
  --criteria records/m3/PROSPECTIVE_CRITERIA.json \
  --output records/m3
```

```bash
git diff --exit-code -- records/m3
```

The validator runs independently of both reconstruction processes, loads each
artifact through `tf.saved_model.load`, audits the graph/checkpoint/signature,
checks all parameter hashes, and executes official A, clean in-memory B,
reloaded C and second-run comparisons over all five frozen fixtures. The final
`git diff` requires regenerated M3 evidence to equal the accepted records.

## 8. Canonical M3 identity and result

The accepted standard TensorFlow SavedModel is at the ignored path
`.m3-work/canonical-h1-savedmodel`:

| Relative path | Bytes | SHA-256 |
|---|---:|---|
| `fingerprint.pb` | 97 | `ce0efa1ec1ac4312d389d8bf785494bb07c200c85a47e40040a34d91e0b21ec5` |
| `saved_model.pb` | 3,100,807 | `d9b844205a70cd9f59b000f3afed287e10d186c4fb7bd4806ef78a969cae82b6` |
| `variables/variables.data-00000-of-00001` | 140,627,678 | `9c32a8d788d2893d38dcdda32de4b261ee8e7ec04514500550700260fd40a99f` |
| `variables/variables.index` | 38,755 | `54ac43bab03edbe554075011c10bf6564c082a18e817eb800ddec0961a3105ae` |

Total bytes: 143,767,337. Aggregate tree SHA-256:
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.
The aggregate is SHA-256 over sorted
`<file_sha256>  <file_bytes>  <relative_path>\n` rows.

Both reconstruction-run trees and all production outputs are byte-identical. The
checkpoint has exactly 673 learned/state tensors plus its object graph. The
full predictor and all H2/H3/dead learned state are absent. The public signature
is `waveform` float32 `[1,96000]` to `scores` float32 `[1,11560]` and
`embedding` float32 `[1,1280]`.

## 9. Evidence and limitations

Read accepted state in this order:

1. `PROJECT_STATUS.md`;
2. the milestone definition and completion report;
3. `records/mN/ACCEPTED_STATE.json`;
4. the milestone evidence manifest;
5. `PAPER.md`.

The five fixtures and their numerical maxima are engineering conformance
evidence, not biological-accuracy validation or universal error bounds. M3
makes no TFLite, quantization, Vela, M55, firmware or Alif hardware claim.
PyTorch and ONNX did not participate in accepted M1–M3. M4 has not started.
