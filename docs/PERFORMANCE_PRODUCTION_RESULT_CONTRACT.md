# H1 prospective production result contract

Status: `PROSPECTIVE_PRODUCTION_RESULT_CONTRACT_V1_FROZEN` — 2026-10-08.
The authoritative [JSON contract](../records/performance/PRODUCTION_RESULT_CONTRACT_V1.json)
defines `PRODUCTION_RESULT_V1` and `QUALIFICATION_DIAGNOSTIC_RESULT_V1`.
This is a product and architecture freeze. Implementation and physical
qualification require separate explicit authorization. No firmware, protocol,
result structure, arithmetic, frontend or hardware changes occurred.

## Authority and current measured state

Entry is `performance/h1-sub700ms` at `dee32eeafc5d2c4b02549e264d24c081ea41973a`,
tree `6a4b8ef7ebfa9d6393cfe216630818d376e23d48`. Published master remains
`c52270fca3f6b7fe2ccece57388bb4040590dd91`.
The immutable [performance criteria](../records/performance/PROSPECTIVE_CRITERIA.json)
SHA-256 is `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b`. Its boundary remains waveform-ready → complete
production ResultReady: 5 warmups, 100 consecutive measured runs, no outlier
removal, every unrounded sample strictly below 700.000 ms.

Preserve lifecycle reuse `4cced1f81517bef0e7dcc556f4dedb9d3c8cfb2f`
(`LIFECYCLE_REUSE_ONLY_PASS`), accepted top-k
`88876bc661cb00c954f8072a369ee37421b8a5f8`
(`POSTPROCESSING_OPTIMIZATION_PASS`), and waveform lifecycle candidate
`092ae308f24457f7852460c4f1227c1c8a3f3273` / implementation
`dee32eeafc5d2c4b02549e264d24c081ea41973a` (`WAVEFORM_VALIDATION_LIFECYCLE_ONLY_PASS`).
The bound waveform receipt SHA-256 is
`14bb0c8e5cc93a87e7a9f21c5f344cb50e35a6664adf0a1f3b20c3e7f3659a05`.
Its four numerical slots are exact; 130 diagnostic/acceptance runs reused input
epoch 1/generation 15 with no repeated waveform CRC, drift, mutation or ownership
failure. Input CRC/declared SHA, lifecycle reuse, top100 and output validation
remain accepted and unchanged.

| Accepted 5+100 metric | ms / count |
| --- | ---: |
| Minimum | 1721.466155 |
| Mean | 1724.3362868 |
| Median | 1724.1813 |
| Nearest-rank p95 | 1726.1007 |
| Maximum | 1726.262335 |
| Population SD | 1.0531192581978833 |
| Samples below 700 / at or above 700 | 0 / 100 |

| Separate measured diagnostic component | Mean ms |
| --- | ---: |
| Diagnostic whole inference (5+20) | 1724.74707225 |
| Frontend | 697.48401125 |
| Unchanged postprocessing CRC/result tail | 479.849042875 |
| Generation/lease checks | 0.0041515 |
| Realized prior waveform lifecycle saving | 133.50051985 |

Latency disposition remains `POST_WAVEFORM_VALIDATION_SUB700_TARGET_NOT_MET`.
No new measurement, subtraction, projected saving or new acceptance is claimed.
All seven CRCs currently participate in ordinary repeat equality and score CRC
is consumed by current RUN/GET_TOPK. Existing eligible result-validation saving
was 0 ms. Those current requirements are not retrospectively redundant.

## Ordinary product result

The prospective product presents three ranked classifications. Named consumers
below are normative future product roles; the current development firmware does
not yet implement a separate top3 product consumer. Present diagnostic coupling
alone does not create a future product requirement.

| PRODUCT_REQUIRED field | Ordinary product consumer |
| --- | --- |
| `contractVersion` | request_controller |
| `status` | request_controller |
| `valid` | classification_presenter and request_controller |
| `resultGenerationId` | request_controller |
| `inputGenerationId` | waveform_window_router |
| `topCount` | classification_presenter |
| `top3[0..2].classIndex` | classification_presenter and label_resolver |
| `top3[0..2].scoreBits` | classification_presenter |
| `modelComponentIdentity` | label_resolver |
| `labelsIdentityAndClassCount` | label_resolver |

The per-inference result carries contract version, status, validity, one result
token, one input token/producer, top count and three index/score-bit pairs.
Success has `valid=1`, `topCount=3`, distinct indices in `[0,11560)`. Failure has
`valid=0`, `topCount=0` and precise status; it cannot reuse the last good result
as the current run. Model/component/labels identity is an immutable session
descriptor available before any classification; it is not rehashed per run.
Status codes preserve the existing domain: 0 NotRun, 1 Ok, 2 InputIdentity,
3 Frontend, 4 Backbone, 5 GeM/bridge, 6 Classifier, 7 Postprocess, 8 diagnostic
Profile, 9 NotReady. Only status 1 can be a valid completed success; 0 cannot be
a completed publication and diagnostic profile validity is not a product
prerequisite. Capture a valid resident input receipt on admission. Missing
identity or an unavailable service before admission is a request rejection.
The currently qualified product authority is uploaded waveform; other resident
producers require separate acquisition/input qualification. Linked fixtures
remain on the diagnostic/legacy route. No new live-audio acceptance is claimed.

`inputCrc32` is PRODUCT_OPTIONAL, omitted from the minimum result. If explicitly
selected prospectively, it comes from the accepted validated receipt and is
complete before Ready. Input validation remains required. Input SHA metadata,
finite/threshold reporting counts, threshold bits, saturation counts, detailed
profile/timing/build/fixture/path flags and diagnostic equality are
DIAGNOSTIC_ONLY. Upload SHA retains its exact declared-metadata meaning; it is
not relabeled as a board-computed uploaded-waveform hash.

Every generated score must still be finite before successful production
completion even though the numeric finiteCount reporting field is diagnostic.
Top3 always contains the three highest accepted scores, including scores below
the existing diagnostic threshold `0x3e19999a`; no implicit filtering,
renormalization or score transformation is introduced. The exact all-score
threshold count and threshold comparison remain available in diagnostics.

Rank using the accepted comparator:

```cpp
scores[left] > scores[right] ||
    (scores[left] == scores[right] && left < right)
```

The score expression remains exactly
`float(int16_logit) * floatFromBits(0x3a08d017)` then
`1.0f / (1.0f + expf(-logit))`. Higher accepted FP32 score first; equal
values use the lower zero-based class index. Preserve original score bits.
No raw-logit ranking, sigmoid substitution or arithmetic tolerance change.
For identical score bits, production top3 must equal accepted top100 entries
0–2 exactly in indices and bits. Both production mismatch counters must be 0.

All 11,560 exact scores may still be computed in the internal workspace.
Ordinary production does not publish or separately preserve the full vector.
Its existing 46,240-byte workspace is not claimed removable. Top100 is
DIAGNOSTIC_ONLY, with its accepted bounded selector preserved. Existing GET_TOPK
publishes the first ten entries; this legacy view is not a top100 publication.

## CRCs, equality and compatibility

| Boundary CRC slot | Source | Production classification |
| --- | --- | --- |
| 0 | frontend output | DIAGNOSTIC_ONLY |
| 1 | backbone input | DIAGNOSTIC_ONLY |
| 2 | shared feature | DIAGNOSTIC_ONLY |
| 3 | embedding | DIAGNOSTIC_ONLY |
| 4 | classifier input | DIAGNOSTIC_ONLY |
| 5 | logits | DIAGNOSTIC_ONLY |
| 6 | scores | DIAGNOSTIC_ONLY |

`scoreCrc32`, `repeat_equal`, `repeat_comparable`, `numericallyEqual()` and the
current `saveResult()` full diagnostic save are DIAGNOSTIC_ONLY under V1.
The score CRC is currently an alias of slot 6, not an eighth independent scan.
Never redefine repeat equality as top3 equality. No `top3_equal` feature is
required. The accepted numerical predicate still compares both valid flags,
input CRC/SHA, finite/threshold counts, score CRC, top count, every boundary CRC
and every stored top100 index/score bit. Existing comparability also requires
the same frontend path. This CRC/result predicate does not replace separate
full-score bit proof.

Existing RUN, GET_TOPK, GET_RESULT_SUMMARY, GET_PROFILE, GET_INFERENCE_DATA,
baseline and UART qualification commands remain available on the explicit
diagnostic/legacy route with their accepted meanings. Their old envelopes and
GET_TOPK top10 view are LEGACY_COMPATIBILITY_ONLY. Future production needs an
explicit distinguishable contract route. No current command meaning, opcode or
wire field changed in this freeze.

`PRODUCTION_RESULT_READY` occurs only after all product-required computation,
accepted input/model/ownership/cache/guard checks, all-score finite validation,
exact sorted top3, generation metadata and complete compact immutable result
publication. Selected optional fields must also be complete. Diagnostic-only
evidence is not a prerequisite; no required product work can follow Ready.
Small production publication remains required before Ready even though the old
diagnostic full-result save is excluded.

## Diagnostic generations and buffer lifetimes

`INFERENCE_GENERATION_ID` is allocated for every admitted inference, including
failure, and is distinct from the reusable waveform token.
`RESULT_GENERATION_ID` equals it. Tokens contain an issuing owner-session,
64-bit epoch and 64-bit inference sequence; the product stores the numeric
result token once. Input identity separately captures waveform epoch,
generation and producer. Old-session tokens cannot be restored/imported after
reset or reinitialization, even if numeric counters repeat. Counters fail
closed before wrap. Existing input lifecycle semantics stay unchanged.

Every `BOUNDARY_BUFFER_GENERATION_ID` is INVALID before write, WRITING during
write, COMPLETE only after full successful stage/ownership checks.
`EVIDENCE_GENERATION_ID` is the diagnostic inference token. Every evidence source
must satisfy:

```text
requested inference generation == result generation
    == source-buffer generation == evidence generation
```

Equality includes owner session and epoch. A COMPLETE source and active
diagnostic read/export pin are also required. Reject stale, overwritten, mixed,
partially updated, incomplete, wrong-session or result/buffer-mismatched
evidence. Check each chunk and final transfer completion; release, cancellation
or timeout retires evidence before another writer. Read pins block all writers.
Numerical comparison between two inference generations binds each side
independently without changing the legacy numerical predicate.

Read-only source and retained-ELF audit found seven persistent PSRAM boundaries:
frontend, backbone input, shared feature, embedding, classifier input, logits
and scores. Each survives successful ResultReady physically until its next
writer, but a later inference overwrites it and can fail partway. Frontend
diagnostics also write frontend/scratch. A boolean `gInferenceDataValid` and
32-bit runSequence provide no complete per-buffer generation/pin proof.
The 400-byte heap index array expires on postprocess return. Materialized
top100 is copied to a 1,880-byte saved result at each save; local result frames
expire on return. The existing first-boundary qualification snapshot is
1,371,328 bytes and is overwritten by the next first-run qualification copy.

Freeze `EXPLICIT_DIAGNOSTIC_INFERENCE_MODE` as the safe architecture.
`ON_DEMAND_GENERATION_BOUND_EVIDENCE` from an ordinary completed product
generation is not authorized. Reject `EVIDENCE_NOT_RETAINED` rather than scan
lingering bytes or silently rerun under its old token. In explicit diagnostic
mode compute CRCs, counts, accepted top100 and complete diagnostic metadata
before DIAGNOSTIC_RESULT_READY; all exact boundary/score bytes are already
computed and remain pinned for export. Serialization can follow diagnostic
Ready. No new multi-megabyte snapshot is introduced to defer CRC work. Future
writer fencing, tags and pins need source coverage and physical qualification;
this is an architecture decision, not implemented runtime safety proof.

Diagnostics retain waveform identity, all numerical boundaries, seven CRCs,
score CRC, all 11,560 exact score bits, accepted top100 indices/bits, counts,
exact repeat semantics and build/profile/qualification identities. Diagnostic
latency may be slower and cannot substitute for production acceptance latency.

## Memory policy and future implementation gates

| Storage category | Current | Prospective ordinary result |
| --- | ---: | ---: |
| Saved result, including prior-repeat authority | 1,880 B | 76 B logical; 80 B illustrative aligned slot |
| Top100 / top3 payload, included above | 800 B | 24 B |
| Boundary + score CRC metadata, included above | 32 B | 0 B |
| Full runtime profile, included above | 912 B | 0 B |
| Full-score array inside saved result | 0 B | 0 B |
| Internal score workspace | 46,240 B | 46,240 B |
| Separate full-score publication retention | Current API aliases workspace | 0 B |
| Existing first full-boundary diagnostic snapshot | 1,371,328 B | No ordinary requirement; diagnostic capacity retained |
| Top-k index stack workspace | 400 B | Potential 12 B; future algorithm/ABI proof |

The previous repeat authority and public saved result are the same gLastResult,
not two persistent 1,880-byte objects. An 80-byte slot gives a prospective
1,800-byte reduction; two small publication slots (160 B) give 1,720 B.
These are result-retention objectives, not implemented savings. Session
descriptor, transient staging, diagnostics and actual ABI/peak memory must be
accounted separately. Reclaiming the existing diagnostic snapshot requires a
separate qualified storage/linker policy; current 1,371,328-byte boundary
workspace, 624,384-byte frontend scratch and NPU classifier output remain.

Future production targets are zero boundary/score evidence full-buffer scans,
zero diagnostic repeat comparisons, and zero diagnostic full-result saves.
Count actual operations and do not hide a scan because its result is discarded.
Source audit also found **one existing stage-internal frontend output CRC scan
over 755,328 bytes** in `frontend.cpp:639`, called by the production wrapper.
It is included in the strict total boundary zero-scan target. Removing the seven
recordBoundaryCrcs scans alone does not satisfy that total target. Frontend CRC
removal is excluded here; report the residual target as unmet, with no timing
subtraction or scope expansion. Product-contract semantic/physical qualification
and zero-scan/performance dispositions are separate. This known source fact
does not authorize a frontend change before the mandatory comparison gate.

Require zero integer mismatches/maximum LSB error, zero full-score bit,
top100-index/score-bit, boundary-CRC, input-identity, and top3-index/score-bit
mismatches. Both named production top3 mismatch counters are also zero.
No tolerance is relaxed. Keep all-score finite/input/model/guard/ownership/cache
work and both U85 stages. Bind exact future source/build/target identities,
test stale/mixed/partial/pin/timeout failures, qualify actual memory, and measure
the unchanged 5+100 waveform-ready-to-production-Ready criterion. This freeze
does not itself qualify an implementation or meet the latency target.

## Mandatory frontend and tiny-birdnet ordering

`FRONTEND_OPTIMIZATION_REQUIRES_TINY_BIRDNET_RECOMPARISON = true`.

1. Separately implement and physically qualify the production-result contract.
2. Reprofile the exact qualified production binary.
3. Freeze a prospective FRONTEND_DECOMPOSITION observation plan.
4. Instrument/decompose the current H1 M55 frontend without optimizing it.
5. Physically qualify the decomposition and its noninterference/exactness.
6. Immediately afterward perform independent `TINY_BIRDNET_M55_RECOMPARISON_V1`.
7. Only after completed comparison may an H1 frontend optimization candidate be selected/frozen.

Before future comparison conclusions bind tiny-birdnet repository/private
locator, remote, branch, commit/tree, dirty/untracked contents, source files,
build system, compiler/toolchain/flags, MCU/core/clock/FPU/MVE/cache, placement,
CMSIS/DSP/library versions, methodology/timing boundaries, sample/window
dimensions, input/output numerical contract, memory report and existing evidence.
If multiple states could be authoritative, STOP to identify one. A known
directory alone is not a selected numerical/performance reference. Only its
location was identified here; no implementation, benchmark or comparison was
inspected or copied.

Algorithm comparison covers windowing, FFT length/type/RFFT-vs-CFFT/kernel,
magnitude/power, mel representation/accumulation, log/compression/normalization,
layout/quantization/copies/scratch/loops/vectorization. M55 comparison covers
scalar/MVE/CMSIS/custom kernels/auto-vectorization, SIMD/alignment/unrolling,
memory/cache/SRAM/PSRAM access, constants/precomputation/tables and any DMA.
Numerical comparison covers samples/window/hop/FFT bins/mel/frequencies,
power/magnitude/epsilon/clamp/log/constants/layout/quantization and
rounding/saturation/output dimensions. Equivalence cannot be inferred from
better timing or a different BirdNET generation.

Timing matrices require exact boundaries, clocks and preferably cycles for total
frontend, spectral/STFT, mel, normalization/log, layout/copies, quantization and
setup. Include validation/CRCs and distinguish omitted work from acceleration.
Memory matrices require matching code, flash/RO, SRAM, PSRAM, stack, scratch,
persistent/FFT/mel/temporary buffers, peak live memory, reuse and allocation
categories. Quantify causes in cycles and bytes rather than assume either path
is optimal or directly reusable.

Classify every advantage as DIRECTLY_APPLICABLE_TO_H1_EXACT_SEMANTICS,
APPLICABLE_WITH_EXACTNESS_PROOF, ARCHITECTURALLY_INFORMATIVE_ONLY,
NOT_COMPARABLE_DIFFERENT_SEMANTICS, MEASUREMENT_BOUNDARY_DIFFERENCE, or
HARDWARE/TOOLCHAIN_DIFFERENCE. Before any later code/table/kernel/constant reuse,
bind provenance and license, classify algorithmic idea/independent
reimplementation/licensed source/generated artifact/external dependency, and
obtain separate explicit reuse/implementation authorization. Inspection alone
does not authorize importing source into the clean lineage.

Required future private deliverables:

- TINY_BIRDNET_REFERENCE_IDENTITY.json
- H1_M55_FRONTEND_DECOMPOSITION.json
- TINY_BIRDNET_FRONTEND_DECOMPOSITION.json
- H1_VS_TINY_BIRDNET_ALGORITHM_MATRIX.json
- H1_VS_TINY_BIRDNET_TIMING_MATRIX.json
- H1_VS_TINY_BIRDNET_MEMORY_MATRIX.json
- H1_VS_TINY_BIRDNET_NUMERICAL_CONTRACT_MATRIX.json
- H1_VS_TINY_BIRDNET_TOOLCHAIN_MATRIX.json
- H1_VS_TINY_BIRDNET_APPLICABILITY_DECISION.json
- TINY_BIRDNET_PROVENANCE_LICENSE_REVIEW.json

The comparison must end as
`TINY_BIRDNET_RECOMPARISON_COMPLETE_FRONTEND_CANDIDATE_JUSTIFIED` or
`TINY_BIRDNET_RECOMPARISON_COMPLETE_NO_DIRECT_FRONTEND_CANDIDATE`.
A specific BLOCKED disposition prevents frontend candidate freeze.

## Scope and stop

No frontend arithmetic/fusion/copy/CRC change, CMSIS/MVE/FFT/mel substitution,
quantization/GeM/NPU/cache/regional/model/Vela/clock/compiler/waveform change,
hardware/DMA CRC acceleration, tiny-birdnet reuse, protocol/result-structure
implementation, hardware action, PR, master update or history rewrite is
authorized by this freeze. Prospective consumer/lifetime/definition/safety/scope
audits are complete; runtime behavior is not implemented or newly qualified.

Next action: separately authorize production-result implementation and physical
qualification against this contract, including exact top3 and diagnostic
preservation, generation safety, memory and truthful zero-scan dispositions.
Then reprofile before mandatory frontend decomposition and tiny-birdnet
re-comparison. Stop now with `IMPLEMENTATION_NOT_STARTED`.
