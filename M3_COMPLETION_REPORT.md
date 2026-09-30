# M3 completion report

Disposition: **ACCEPTED**.

M3 serialized the accepted clean TensorFlow FP32 H1 as a standard SavedModel at
the ignored local path `.m3-work/canonical-h1-savedmodel`. The public
`serving_default` signature accepts `waveform` float32 `[1,96000]` and returns
named `scores` float32 `[1,11560]` and `embedding` float32 `[1,1280]`.

The canonical tree has four files, totals 143,767,337 bytes and has aggregate
SHA-256
`4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a`.
Its files are:

| Relative path | Bytes | SHA-256 |
|---|---:|---|
| `fingerprint.pb` | 97 | `ce0efa1ec1ac4312d389d8bf785494bb07c200c85a47e40040a34d91e0b21ec5` |
| `saved_model.pb` | 3,100,807 | `d9b844205a70cd9f59b000f3afed287e10d186c4fb7bd4806ef78a969cae82b6` |
| `variables/variables.data-00000-of-00001` | 140,627,678 | `9c32a8d788d2893d38dcdda32de4b261ee8e7ec04514500550700260fd40a99f` |
| `variables/variables.index` | 38,755 | `54ac43bab03edbe554075011c10bf6564c082a18e817eb800ddec0961a3105ae` |

Two independent reconstruction runs used the same deterministic constructor
implementation in separate CPython 3.12.14 / TensorFlow 2.21.0 processes and
emitted byte-identical complete trees. This is execution independence, not a
claim that two separately implemented constructors exist.

A controlled pair of otherwise identical pre-normalization saves established
that only protobuf string field 7, `FingerprintDef.uuid`, differed in
`fingerprint.pb`. The five content fingerprint fields
(`saved_model_checksum`, `graph_def_program_hash`, `signature_def_hash`,
`saved_object_graph_hash`, and `checkpoint_hash`) and `version.producer` were
equal, as were `saved_model.pb` and both checkpoint files. TensorFlow 2.21
generated a random decimal UUID string on each save.

Immediately after `tf.saved_model.save` returns and before tree identity is
computed, `normalize_fingerprint()` parses `fingerprint.pb`, forms sorted
`name=value` lines from those five fields plus `producer`, hashes that text
with SHA-256, interprets the first 16 digest bytes as an unsigned big-endian
integer, and assigns its decimal representation to `FingerprintDef.uuid`.
For this artifact the normalized UUID is
`52591591100515463165678265775315934915`. It then deterministically
re-serializes only `fingerprint.pb`.

The normalization does not write `saved_model.pb`, either checkpoint file, or
the external labels. Therefore it changes no graph computation, embedded
StableHLO program, retained-variable set, variable value, input/output
signature, or label order. Independent audit confirms those identities, and
both canonicalized artifacts load normally through `tf.saved_model.load`.
No acceptance threshold or fixture changed.

An independent process reloaded both artifacts through `tf.saved_model.load`.
The checkpoint contains exactly one object graph entry and 673 learned/state
tensors in `h1_variables/0..672`; all 673 official/canonical/rebuild value hashes
match. The only XlaCallModule payload has SHA-256
`3cff5699feedb7cde9c877f88ecaefc9c73da44fb735875411f5fdd10f78ecda`.
The full predictor, H2/H3 live computation, H2/H3 learned state and other dead
learned state are absent.

Across all five frozen fixtures, every A/B probe comparison and every
official/in-memory/reloaded/rebuild production comparison was raw-byte equal.
Observed worst maximum absolute error, mean absolute error and RMSE were all
`0.0`; all nonfinite masks agreed. Two fresh validation processes also emitted
byte-identical retained evidence.

Static import inspection and runtime module inspection found no `torch`,
`torchvision`, `torchaudio`, `onnx` or `onnxruntime` participation. The M3
adversarial review has no `UNRESOLVED` item. Exactly one immutable future-M4
binding is retained in `records/m3/CANONICAL_M4_FP32_INPUT.json`.

Authoritative compact evidence:

- `records/m3/ARTIFACT_IDENTITY.json`
- `records/m3/SERIALIZED_AUDIT.json`
- `records/m3/RELOAD_VALIDATION.json`
- `records/m3/DEPENDENCY_CHECK.json`
- `records/m3/ADVERSARIAL_REVIEW.json`
- `records/m3/CANONICAL_M4_FP32_INPUT.json`
- `records/m3/ACCEPTED_STATE.json`

M4 entry is authorized. M4 has not started.
