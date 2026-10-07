# H1 waveform validation lifecycle candidate

Candidate class: **WAVEFORM_VALIDATION_LIFECYCLE_ONLY**.
Status: **PROSPECTIVE_WAVEFORM_VALIDATION_LIFECYCLE_CANDIDATE_V1_FROZEN**.
Frozen: 2026-10-07. The machine-readable authority is
[WAVEFORM_VALIDATION_LIFECYCLE_CANDIDATE_V1.json](../records/performance/WAVEFORM_VALIDATION_LIFECYCLE_CANDIDATE_V1.json).

This authority defines a future uploaded-input generation lifecycle. This session
freezes, signs and pushes only the candidate and this companion. **STOP BEFORE
IMPLEMENTATION.** Firmware changes, builds, programming, hardware actions and new
inferences require separate authorization.

## Bound authorities and measured opportunity

| Authority | Exact identity / disposition |
| --- | --- |
| Entry and observer implementation | `81d7adea02978b5aa0648606eb7c3d1e9c9b24e6` — `Instrument H1 hot-path validation` |
| Performance branch | `performance/h1-sub700ms`; entry remote equals observer |
| Published master preserved | `c52270fca3f6b7fe2ccece57388bb4040590dd91` |
| Main criteria SHA-256 | `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b` |
| Lifecycle implementation | `4cced1f81517bef0e7dcc556f4dedb9d3c8cfb2f` — `LIFECYCLE_REUSE_ONLY_PASS` |
| Top-k implementation | `88876bc661cb00c954f8072a369ee37421b8a5f8` — `POSTPROCESSING_OPTIMIZATION_PASS` |
| Observation-plan commit | `d65b6949b7f010b5b23527335bc30703a0ba0598` |
| Observation-plan SHA-256 | `ea4e6639910896acc78ce7b992fdd7128f021f89c11fd7664d89fb057dfd15b8` |
| Qualified observation | `HOT_PATH_VALIDATION_MEASUREMENT_QUALIFIED` |
| Combined candidate | `COMBINED_HOT_PATH_VALIDATION_LIFECYCLE_CANDIDATE_NOT_JUSTIFIED` |

The observation plan and observer were locally GOOD SSH signed and GitHub
Verified. The nominated qualified report/receipt is
`HOT_PATH_VALIDATION_QUALIFIED_20261007T154543Z.json`, SHA-256
`81ec9b62483d1b75a9225fd3b52458695d12be64add72c1c63fc4d380b009ef1`.
Its measurement qualification receipt has SHA-256
`37b73b46d7d12b277919ddeb9e4901b1c1fc05e5667159dcff75679e4837e90c`.
The candidate binds exact compact identities for W/R statistics, logical scan
ledgers, consumer and buffer-lifetime audits, campaign receipt and accepted
post-top-k timing. Original evidence stays unchanged; raw records are retained
privately and are not copied into this authority.

All 20 measured records passed structural qualification, complete timestamps,
exact byte/call counters and exact W/R reconciliation. None was removed.

| Qualified quantity | Value |
| --- | --- |
| Full resident waveform CRC bytes / successful inference | 384,000 |
| Full resident waveform CRC calls / successful inference | 1 |
| Full waveform CRC mean | 134.403990625 ms |
| CRC p95 | 134.404898 ms; unrounded 134.4048975 ms |
| CRC maximum | 134.404980 ms |
| W0 mean | 134.415010 ms; unrounded 134.41500975 ms |
| W residual / reconciliation error | 0 cycles in every measured run |
| R0 mean | 479.549119 ms |
| Seven output CRC mean | 479.525781 ms; unrounded 479.52578125 ms |
| Eligible output validation removal | **0 ms** |

The conditional arithmetic is exactly:

`1857.83680665 - 134.403990625 = 1723.432816025 ms`.

This subtracts only the measured repeated resident waveform scan from the
accepted post-top-k mean. It is a projection, not measured optimized performance
or a PASS. Diagnostic scan intervals include observer overhead. New generation,
state, ownership and lease checks have nonzero, unmeasured cost. Approximately
1723 ms is context for later observation, not an acceptance requirement.

## Generation ownership

**VALIDATE EACH INPUT GENERATION WHEN IT BECOMES VALID. DO NOT RESCAN AN UNCHANGED
VALID GENERATION ON EVERY INFERENCE.**

Use the existing single resident waveform buffer and separate transport staging
payload. Do not introduce double buffering. Keep the current single application
command owner; parser admission and resident writers serialize with inference
input consumption. Traffic queued in other transport buffers cannot mutate or
relabel a leased generation.

| State | Meaning |
| --- | --- |
| EMPTY | New runtime; no generation receipt or lease. Retained bytes confer no validity. |
| WRITING | An admitted attempt or mutator owns fresh generation G; old receipt revoked; no inference lease. |
| UNVALIDATED | Required bytes complete and mutators quiescent; exclusive validation control; no lease. |
| VALID | Complete receipt atomically published for G; no active writer; resident bytes immutable. |
| INVALID | Failed, cancelled, aborted or retired generation; no receipt/lease and no return to VALID for that ID. |

`WAVEFORM_GENERATION_ID` is an opaque runtime epoch plus an internally owned,
monotonic unsigned 64-bit counter. It is independent of upload/request sequence,
CRC, SHA and content equality. Every admitted recognizable upload attempt and
other resident mutation intent consumes a fresh value, including failed attempts,
identical bytes and reused host sequence. One attempt keeps its ID across
staging, copy, validation and publication. Counter wrap/recycling is forbidden;
exhaustion fails closed.

The runtime epoch scopes equality to the current runtime instance. Reset or new
initialization makes all old receipts and tokens unusable and starts EMPTY, even
if PSRAM bytes or numeric counter values repeat. Never persist/restore old input
validity. Host-issued or pre-reset tokens cannot establish current validity.

The receipt binds ID/epoch, producer kind, exact resident base/count/bytes and
format, completion/errors, required finite/canonical checks, upload CRC and
retained declared SHA, and publication/ownership state. Publish these together
with VALID after validation and mutator quiescence.

Every path that can write any resident byte must first obtain exclusive mutation
admission with no input lease, revoke the old VALID receipt and clear compatible
upload/acquisition validity flags. Bind a fresh producer-specific generation
before its first store. No inference can lease WRITING, UNVALIDATED, INVALID or
EMPTY. Full producer validation is required before publication. Failed IDs cannot
be rehabilitated; retry is a new generation.

## Upload validation and failure semantics

At the first parser recognition of an UploadWaveform command, at latest the
complete identifying header and before payload reception, create G and invalidate
the old receipt. Unsupported version/length, frame rejection, partial receive,
abort, disconnect and parser reset must be covered. Unrecognizable/truncated
framing never publishes a generation. This uses the existing staging buffer and
deliberately retains no old-input fallback.

The prospective route is:

1. Upload begins: fresh G, revoke old validity, WRITING before mutation/staging.
2. Receive complete payload: exactly 384,060 bytes, consisting of 60 metadata and
   384,000 raw bytes. Complete staging becomes UNVALIDATED.
3. Preserve current framing/version/payload CRC and metadata checks: dtype 1,
   rank 2, shape `[1,96000]`, 32,000 Hz and 384,000 raw bytes. Retain declared raw
   CRC and 32 declared SHA bytes as the current 64-character hex metadata.
4. Run the existing full raw payload CRC and require declared raw CRC equality.
5. Under exclusive ownership for the same G, transition through WRITING for the
   complete unchanged resident copy, then UNVALIDATED with no active writer.
6. Require all 96,000 resident float values finite. Check actual resident CRC
   against the qualified raw/declared CRC once before publication; preserve the
   existing canonical CRC/SHA/byte discriminator where its conditional test
   applies.
7. Bind the complete receipt to G, publish VALID/waveform-ready, and lease G for
   repeated inference using bounded generation/state/ownership checks only.

Actual resident identity must be qualified; a staged CRC or the current
`gUpload.valid` boolean alone is insufficient. Keep one existing raw-payload
384,000-byte CRC scan when that check is reached, and one resident 384,000-byte
CRC scan at generation publication. The latter relocates the earliest legacy
resident check. For successful upload plus first inference this corresponds to
the existing raw scan plus first resident scan; no arbitrary extra raw pass or
full SHA scan is required. Early-rejected generations may reach neither check
and cannot become VALID. Record actual work on failures.

Uploaded SHA is retained host-declared metadata today. It is not board-computed
or cryptographically verified against waveform content. Preserve that contract
and its exact echo. Preserve `canonicalByteMatch` and canonical-required consumer
guards. Generic finite noncanonical uploads remain permitted; false
`canonicalByteMatch` does not itself reject generic upload. The zero identity
mismatch gate covers required raw/resident CRC and required fixture identity,
without adding a new content-SHA validation contract.

| Future observable counter | Required meaning |
| --- | --- |
| UPLOAD_GENERATIONS_CREATED | One increment / admitted recognizable attempt, including failures |
| UPLOAD_GENERATIONS_VALIDATED | One increment / successful complete receipt publication |
| FULL_RAW_WAVEFORM_CRC_SCANS_PER_NEW_UPLOAD_GENERATION | 1 when existing raw check is reached, otherwise 0 |
| FULL_RAW_WAVEFORM_CRC_BYTES_PER_NEW_UPLOAD_GENERATION | 384,000 for completed raw check; actual partial work on failure |
| FULL_RESIDENT_WAVEFORM_CRC_SCANS_PER_VALIDATED_UPLOAD_GENERATION | 1 before successful publication |
| FULL_RESIDENT_WAVEFORM_CRC_BYTES_PER_VALIDATED_UPLOAD_GENERATION | 384,000 |
| VALID_GENERATIONS | Cumulative publications, at most one / ID |
| INVALID_GENERATIONS | Cumulative terminally failed/retired IDs, counted once / ID |
| CURRENT_WAVEFORM_GENERATION_STATE | State, producer, ID, writer/lease ownership and receipt binding |

Require zero `PARTIAL_UPLOAD_ACCEPTED`, `FAILED_FRAME_CRC_ACCEPTED`,
`INVALID_METADATA_ACCEPTED`, `RAW_CRC_MISMATCH_ACCEPTED`,
`NONFINITE_INVALID_UPLOAD_ACCEPTED` and `IDENTITY_MISMATCH_ACCEPTED` under the
current contract described above. A partial attempt remains non-VALID; abort or
failure terminates it as INVALID. Never restore the old receipt or present old
bytes as the new successful upload. Previously published result retrieval keeps
its existing ordinary behavior; input invalidation does not redefine output
publication or repeat comparison.

## Complete writer audit

The exact source has two direct resident writers. Neither has a complete current
generation contract. This is a prospective ownership design, not a current
safety PASS. The observer binary has audio backend NONE, so the PDM writer is
source-present and excluded from that exact build.

| Path / backend | Current assessment and required future handling |
| --- | --- |
| UploadWaveform command 4, `validateUpload` resident `memcpy` | Clears gUpload before copy and publishes after validation; parser-level early failures can retain old validity. Add attempt-start, failure and generation receipt ownership. |
| PDM `MicRunWindow` command 16, `runMicUsbCommand` -> `h1AudioPdmPrepareSelectedWaveform` conversion | Writes shared resident floats without clearing gUpload.valid. Common invalidation and exclusive mutation admission before conversion are mandatory; no conversion arithmetic or new live acquisition work. |
| USB upload reception | Writes existing separate uploadPayload staging, not resident storage. Must create/invalidate the attempt before receive and handle every failure/partial exit. |
| Legacy uploaded inference command 49 | Uses the same resident upload; no second upload writer. Must use the same receipt/lease gates, including legacy frontend consumption. |
| Spectral/full/legacy frontend diagnostics and potential injection | Current inputs are const readers with separate scratch/output. Protect input ownership; any new injection is a generation-owned writer. |
| Canonical/fixture loading, RunCanonical and UART C/G | Current linked fixtures are separate const inputs, not resident loaders. Preserve exact build and fixture checks; no optimization of independent linked-fixture routes. Future resident loading must join generation ownership. |
| Line/I2S | Only conditional dispatch stubs at this HEAD; no implemented audio_i2s backend or selectable I2S build. Define completion/ownership contract only; audit before later activation. |
| Raw-memory read diagnostic | Reads volatile const memory; writes diagnostic metadata only. No firmware raw resident write command found. Any future raw write must be owned or qualification stops. |
| Mutable accessor / exported storage | `h1UploadedWaveform()` returns float* and storage has an exported symbol. All callers/aliases must be audited and mutable access confined to admitted writers. |
| Boot/startup storage | Starts EMPTY; no validity restored from retained bytes. Any explicit clear/initialization write is an owned mutation. |
| External debugger/probe/programmer memory writes | Arbitrary writes cannot be reliably observed by firmware. Revoke receipt and establish exclusive ownership before separately authorized writes; halt alone is insufficient. Uncoordinated writes destroy qualification and cannot reuse old receipts. |

Disposition: **WAVEFORM_WRITER_AUDIT_COMPLETE**. No fundamental current source
blocker prevents specifying this simple ownership contract. Future implementation
must repeat the exact source/binary audit and STOP if any writer or alias cannot
be covered without scope expansion. In particular, excluded builds do not make
unsafe PDM aliases eligible to bypass ownership.

## Inference lease and result identity

At waveform-ready START, input must already be VALID with the right producer,
exact receipt/buffer binding and no mutation owner. Keep START at its existing
location. Acquire/check and snapshot at most one input lease inside the timed
route before frontend consumption. No writer or parser admission may relabel or
mutate G until that consumption completes. Recheck token/state/ownership after
consumption and release safely within the measured route, including error exits.

Copy `result.inputCrc32` from the leased, already validated resident/raw CRC and
`result.inputSha256` from that generation's exact retained declared SHA metadata.
The immutable VALID generation makes these values invariant. Require exact legacy
versus future field equality for the same uploaded generation:
`INPUT_IDENTITY_FIELD_MISMATCHES = 0`. Never remove or reinterpret the fields.
Later input retirement cannot relabel a result's leased snapshot.

Lease checks, metadata handling, synchronization and release have nonzero cost.
They remain within the existing waveform-ready -> ResultReady route. Initial
upload validation precedes waveform-ready because readiness requires VALID;
this does not permit moving repeated checks before START or after ResultReady.
No full resident scan occurs in repeated inference, including hidden per-run
validation outside the timer.

Require zero `INFERENCE_WITH_INVALID_GENERATION`,
`INFERENCE_WITH_WRITING_GENERATION`,
`GENERATION_CHANGED_DURING_INPUT_CONSUMPTION` and `STALE_WAVEFORM_ACCEPTED`.

## Future live-input validity

`ACQUISITION_GENERATION` is producer-tagged in the same resident ownership domain
and bound to an acquisition/DMA or selected-window completion token. Live content
has no predetermined identity. Explicitly freeze
`LIVE_AUDIO_EXPECTED_CONTENT_CRC_REQUIRED = false`; expected waveform CRC,
declared waveform SHA and known fixture identity are not microphone/line validity
requirements.

VALID requires completed acquisition/DMA and selected snapshot/conversion, exact
sample and byte counts, format/channel/sample-rate compliance, correct stable
buffer ownership, no active writer of that generation, no acquisition error and
no FIFO/DMA/queue/slab/overrun error invalidating it, applicable bounds/finite
checks, and atomic generation publication. The current H1 resident representation
is 96,000 mono float32 samples / 384,000 bytes at 32,000 Hz; current PCM16 windows
are 96,000 samples / 192,000 bytes. Any later line representation requires its own
exact authorized format/channel contract. Acquisition into other ring/slab
buffers is permitted only without aliasing the leased resident generation.

Ordinary inference then uses the same valid-generation lease. CRC/SHA may later
serve transport integrity, diagnostics or existing ordinary observed fingerprints
per completed generation. They do not become pre-known content validity checks.
Existing PDM calculated CRC/SHA are observed identities, never a fabricated
host-declared SHA. Future live result availability/representation and any product
change need separate authorization. This candidate defines these semantics and
minimal ownership hooks only; it implements no live input and silently changes
no current live fields.

## Future proof and acceptance gates

Freeze all four repeated-inference structural gates at zero:

- `FULL_WAVEFORM_CRC_SCANS_PER_MEASURED_INFERENCE`
- `FULL_WAVEFORM_CRC_BYTES_PER_MEASURED_INFERENCE`
- `FULL_WAVEFORM_SHA_SCANS_PER_MEASURED_INFERENCE`
- `FULL_WAVEFORM_IDENTITY_VALIDATIONS_PER_MEASURED_INFERENCE`

Use the accepted four slots: canonical, canonical immediate repeat,
high_valid_two_tone and its immediate repeat. Require exact waveform bytes at
frontend, native quantized backbone input, backbone output, classifier input and
output, all 11,560 score bits, top100 indices and score bits, finiteCount,
thresholdCount, topCount, all seven boundary CRCs, score CRC and input identity
fields. Hard gates are all zero: `TOTAL_INTEGER_MISMATCHES`,
`MAXIMUM_LSB_ERROR`, `FULL_SCORE_BIT_MISMATCHES`, `TOP100_INDEX_MISMATCHES`,
`TOP100_SCORE_BIT_MISMATCHES`, `BOUNDARY_CRC_MISMATCHES` and
`INPUT_IDENTITY_FIELD_MISMATCHES`. No tolerance or approximation is introduced.

Prove same-generation reuse, both immediate repeats exact, no generation drift,
revalidation scan, buffer mutation or ownership violation. One uploaded generation
may serve all 105 acceptance runs after prevalidation:
`FULL_WAVEFORM_CRC_SCANS_ACROSS_105_STEADY_STATE_RUNS = 0`.

Preserve `LIFECYCLE_REUSE_ONLY_PASS` and `POSTPROCESSING_OPTIMIZATION_PASS`, exact
heap selector ordering and every accepted model/storage/guard/route/cache/build
configuration gate. Keep zero model payload copy bytes, interpreter constructions
and AllocateTensors calls per inference. Require zero CPU_FALLBACK, FAULTS,
GUARD_FAILURES, UNEXPECTED_RESET, PERSISTENT_MODEL_MUTATION, QUEUE_IRQ_FAILURES,
GENERATION_OWNERSHIP_FAILURES and INVALID_GENERATION_INFERENCES.

Future adversarial checks must cover partial/aborted/disconnected upload, malformed
framing, frame CRC failure, metadata/length/format errors, raw/resident identity or
copy failure, nonfinite input, required canonical mismatch, failures before/after
copy, identical content/reused host sequence with fresh IDs, PDM invalidation of
old upload ownership, blocked writers during leases, stale/reset tokens,
mid-consumption drift and counter exhaustion. Preserve valid noncanonical uploads
and the existing SHA declaration contract.

After separate implementation/physical authorization and exact binary binding,
first run 5 warmups + 20 measured diagnostics, retaining every raw sample. Confirm
zero repeated scans, measure generation/lease cost, verify unchanged output CRCs,
exact numerical/input identities, lifecycle/top-k and runtime/ownership gates.
Then the **same exact binary** runs 5 warmups + 100 consecutive measured samples;
retain all, remove no outliers. Main criteria remain unchanged: **every unrounded
sample <700.000 ms**. Actual measurements control the result.

A later `WAVEFORM_VALIDATION_LIFECYCLE_ONLY_PASS` requires every candidate gate
and may coexist with latency above 700 ms. It is distinct from
`H1_REPEATABLE_SUB700MS_PERFORMANCE_PASS`. If actual measurements miss the main
gate, report `POST_WAVEFORM_VALIDATION_SUB700_TARGET_NOT_MET`; the projection does
not predetermine the outcome. This freeze issues no implementation/performance
PASS and no formal M4/M5 acceptance.

## Output preservation, scope and stop

Preserve current post-P0 behavior unchanged: `boundaryCrc32[0..6]`, `scoreCrc32`,
`repeat_equal`, `numericallyEqual()`, `saveResult()`, result publication,
ResultReady, the full score buffer and top100. All seven CRCs participate in
ordinary repeat comparison; score CRC also participates in RUN/GET_TOPK. Their
measured 479.525781 ms stays in the hot path. Attribute **zero output saving**.

Future source changes are limited to necessary input generation/state/receipt
metadata, writer invalidation and upload-completion binding, inference lease
checks, reuse of validated CRC/retained declared SHA in existing result fields,
minimal structural counters, and live ownership hooks without acquisition
implementation. Preserve waveform/model/boundary storage layout and ownership;
minimal bounded metadata requires separate qualification within existing memory
capacity. No storage relocation for speed or arithmetic path changes.

Exclude production top-3, regional/classifier projection, frontend optimization,
fusion or copy elimination, GeM/U85 optimization, cache optimization,
stage-boundary zero-copy, model/Vela, clock/compiler flags, CRC algorithm
acceleration, hardware/DMA CRC, boundary CRC relocation, result-contract changes,
new double buffering and live acquisition implementation.

STOP on evidence/entry identity mismatch; any uncovered writer/alias or unsafe
invalidation; inability to qualify actual resident copy or preserve uploaded
identity fields; required output, START, arithmetic, frontend/cache/NPU/model/Vela
or other excluded changes; incorrect expected-content live validity; failed
future hard gates or binary mismatch. Do not broaden scope or relax identities,
tolerances, counters or build/safety gates to rescue the candidate.

SSH freeze fingerprint:
`SHA256:vwwTKHP0Mce4nGalzVyoddmZWIHHdlQW2crs5hx05gs`.
Commit only this companion and the candidate JSON; subject
`Freeze H1 waveform-validation lifecycle candidate`. Require locally GOOD and
GitHub Verified; push only `performance/h1-sub700ms`. No PR, merge, master change
or history rewrite is authorized. Commit identity is recorded separately because
a commit cannot contain its own hash.

**STOP BEFORE WAVEFORM_VALIDATION_LIFECYCLE IMPLEMENTATION.**
