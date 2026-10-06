# H1 repeatable sub-700 ms performance criteria

This is post-M5/post-release engineering for steady-state production H1 inference. The user specified the strict **<700.000 ms** threshold prospectively. These criteria were created before new performance measurement, with no candidate timing data consulted. This freeze issues no performance pass and contains no measurement results.

[`records/performance/PROSPECTIVE_CRITERIA.json`](../records/performance/PROSPECTIVE_CRITERIA.json) is the machine-readable authority (schema `birdnet-h1-prospective-performance-criteria-v1`, version 1). This document must agree with it.

## Frozen identity

| Binding | Identity |
| --- | --- |
| Created | 2026-10-06 |
| Performance branch | `performance/h1-sub700ms` |
| Published master base | `c52270fca3f6b7fe2ccece57388bb4040590dd91` |
| Published master parent | `f2293668e657c0659fad075e4f69fdfd063f7432` |
| Published master tree | `73e663007ffbace297b66a9abd68ced687134fdb` |
| Published release content SHA-256 authority | `413fa44e9375f184a5b7b2714af40709430051664bcae946e33baaa33f60e39a` |
| Attribution commit | `cf0778692922d3fa2e89d32ce83ac177b5d2ea5a` |
| Exact criteria file SHA-256 | `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b` |

The criteria SHA-256 is immutable for this stage. Any later change requires a new prospectively versioned criteria file and an explicit reason; never edit this frozen file after observing benchmark results.

Historical M5 remains **ACCEPTED**. Historical Matter provenance remains `MATTER_BINDING_UNRESOLVED_STOP`; the published clean candidate remains `NO_MATTER_PUBLIC_CANDIDATE_QUALIFIED`. Their acceptance records are not reopened or weakened. N1 remains `POST_RELEASE_FOLLOWUP_NONBLOCKER`: the obsolete `validate_identity()` / `validate-canonical` implementation in `scripts/h1_usb_client.py` is left unchanged in initialization.

## Primary production latency

**START:** A complete valid 3-second mono waveform is already resident in the production waveform/input buffer and the inference request is ready to begin.

**END:** The final H1 inference result required by the ordinary production reporting path is ready in firmware memory for reporting.

Include all per-inference work after START:

- Frontend processing.
- Frontend quantization.
- All production stage handoffs.
- CPU memory copies.
- Cache clean/invalidate operations required by the run.
- U85 backbone execution.
- Backbone-output ownership transition.
- M55 GeM.
- Exact classifier bridge.
- Classifier-input ownership transition.
- U85 classifier execution.
- Result conversion/postprocessing required by ordinary production reporting.

Exclude:

- USB upload/host transport.
- Live audio acquisition time.
- Cold boot.
- Firmware startup.
- One-time model loading/copy.
- Model hash verification performed only at initialization.
- One-time interpreter construction.
- One-time NPU initialization.
- Debug export/capture not part of ordinary production execution.

Only work actually performed once at initialization is excluded. Model copies, checks or reconstruction performed per inference remain inside the interval and must satisfy the structural gates.

Do not stop timing at the classifier IRQ or raw logits if ordinary reporting still requires conversion or postprocessing.

## Repeatability campaign

Use the canonical valid 3-second mono waveform already used by the accepted uploaded-waveform route. It must be resident before START: 32,000 Hz, little-endian float32, 96,000 elements, 384,000 bytes, SHA-256 `23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8`.

- Warmups: **5 consecutive successful production-path runs**, immediately before the measured sequence.
- Measured runs: **100 consecutive completed production-path runs**. Failed, incomplete or missing runs fail the campaign.
- Outlier removal: **NONE**. Retain every sample.
- Hard gate: **every measured `end_to_end_latency_ms < 700.000`**, equivalently **`max_ms < 700.000`**. Compare unrounded samples. Equality at 700.000 ms fails; `<=700` is not allowed.

Report at minimum:

| Metric | Frozen definition |
| --- | --- |
| `count` | 100 measured latency samples; warmups excluded from measured statistics. |
| `minimum_ms` | Minimum of all measured samples. |
| `mean_ms` | Arithmetic mean of all measured samples. |
| `median_ms` | Mean of the 50th and 51st samples in ascending order. |
| `p95_ms` | Nearest-rank percentile: ceil(0.95 * count), the 95th ascending sample for count 100. |
| `maximum_ms` | Maximum of all measured samples. |
| `population_standard_deviation_ms` | Square root of sum((sample - mean)^2) / count; denominator 100. |

Preserve all 100 raw per-run latency samples privately with run identifiers, timestamp/cycle data and clock conversion identity.

Compact aggregates plus bound identity/procedure information sufficient to reproduce or inspect the campaign; no raw captures or restricted payloads.

## Timing integrity

### Acceptance timing mode

Minimal; must not materially distort production timing or behavior.

Allowed inside/around timing:

- High-resolution monotonic timestamp/cycle reads.
- Required lightweight run identifiers.
- Lightweight counters that do not materially alter production behavior.

Forbidden inside the primary interval:

- USB tensor dumps.
- Complete tensor CRC sweeps not part of production.
- File/host transfers.
- Large printk logging.
- Debug copies.
- Full tensor comparisons.
- Expensive PMU reconfiguration.

Bind clock frequency and timestamp conversion, resolution and wrap handling; compare unrounded latency to the strict threshold.

### Diagnostic profiling mode

Use a separate campaign for richer stage timings, PMU counters and transfer/cache information. V1 does not allow diagnostic latency to substitute for acceptance timing. Any exception requires independently demonstrated negligible overhead and an explicitly permitting prospectively versioned criteria revision.

## Required diagnostic stage profile

Measure these boundaries before optimization decisions:

| Stage | Boundary | Processor |
| --- | --- | --- |
| 1 | Frontend spectral/STFT/power | CPU |
| 2 | Compact mel | CPU |
| 3 | Remaining frontend normalization/resize/quantization | CPU |
| 4 | Frontend -> backbone handoff | CPU/handoff |
| 5 | U85 backbone submit -> completion | U85 |
| 6 | Backbone -> GeM ownership/handoff | CPU/handoff |
| 7 | M55 GeM | CPU |
| 8 | GeM/classifier bridge | CPU |
| 9 | Classifier-input handoff | CPU/handoff |
| 10 | U85 classifier submit -> completion | U85 |
| 11 | Logits/result postprocessing | CPU |
| 12 | Total waveform-ready -> result-ready | End-to-end |

For CPU stages, report cycles and microseconds/milliseconds where reliable.

For each U85 stage, report separately where available:

- Submit -> completion wall time.
- NPU cycle count.
- NPU active.
- MAC active.
- Weight-decoder active/stalled.
- External-read transaction stalls.
- SRAM transaction stalls.

Record availability and reason for any unavailable or unreliable metric; preserve the required stage boundaries.

Historical D512 timing is architectural background only and must not substitute for current H1 baseline measurements.

## Baseline memory-transfer ledger

Observe the baseline ledger before changing memory ownership or layout. Every production transfer/handoff requires:

- Producer stage.
- Consumer stage.
- Source address.
- Source region.
- Destination address.
- Destination region.
- Element type.
- Element count.
- Bytes.
- Mechanism.
- Transfers per inference.
- Total bytes moved per inference.
- Cache state before transition.
- Cache clean bytes.
- Cache invalidate bytes.
- Whole-cache versus range maintenance.
- Lifetime start.
- Lifetime end.
- Alignment.
- Whether producer and consumer can legally share the same physical buffer.
- Reason if a copy is mandatory.

Mechanisms: Direct alias, CPU memcpy, Loop copy, DMA, NPU write, Other.

Required major boundaries:

- Waveform -> frontend.
- Frontend -> backbone.
- Backbone -> shared feature / GeM.
- GeM -> classifier bridge.
- Bridge -> classifier input.
- Classifier -> reporting.

Audit every full-size frontend intermediate tensor.

Create candidate zero-copy opportunities only after the baseline ledger is observed. Bind hardware/runtime accessibility, cache ownership and live-buffer lifetime evidence for each legally shared buffer and every proposed removed copy.

## Structural performance gates

1. No per-inference model payload copy.
1. No per-inference interpreter/model reconstruction.
1. No CPU fallback from either U85 stage.
1. Model identities remain fixed during the campaign.
1. Required cache coherency remains correct.
1. No memory guard/lifetime regression.
1. No numerical boundary regression relative to the applicable qualified authority.
1. Every removed copy is replaced by an explicitly proven ownership/lifetime transition.

Zero-copy means **One producer -> one physical boundary buffer -> one consumer, where the hardware/runtime contract permits it.** Unavoidable U85 weight/activation memory traffic is not called zero-copy.

## Investigation order: nonbinding guidance

This order is not an acceptance gate:

1. Detect/eliminate any per-run model copy/reinitialization.
1. Frontend writes directly to backbone input where legal.
1. Backbone output consumed in place by GeM where legal.
1. GeM/bridge writes directly to classifier input where legal.
1. Classifier output processed in place where legal.
1. Replace unnecessary whole-cache operations with validated range-specific maintenance.
1. Remove production-path diagnostic copies/CRCs not required for ordinary inference.
1. Remeasure U85 PMU/external-memory stalls.
1. Only if required, investigate U85 staging/fast-memory changes.
1. Only after simpler transfer changes, consider frontend fusion, GeM arithmetic optimization or postprocessing changes.

Observed baseline data decides which candidates proceed; this order does not predetermine results.

## Numerical/equivalence guard

Classify each candidate changing production source behavior before implementation:

| Candidate class | Required gate |
| --- | --- |
| `MEMORY_PLACEMENT_ONLY` | Applicable exact integer boundary preservation and existing qualified floating-scope preservation. |
| `COPY_ELIMINATION_ONLY` | Applicable exact integer boundary preservation and existing qualified floating-scope preservation. |
| `CACHE_MAINTENANCE_ONLY` | Applicable exact integer boundary preservation and existing qualified floating-scope preservation. |
| `INSTRUMENTATION_ONLY` | Applicable exact integer boundary preservation and existing qualified floating-scope preservation. |
| `ARITHMETIC_IMPLEMENTATION_CHANGE` | Separately defined prospective equivalence/requalification gate before candidate acceptance. |
| `MODEL/VELA_CHANGE` | Separately defined prospective equivalence/requalification gate before candidate acceptance. |

Applicable qualified H1 numerical/runtime authority bound through the published master and accepted records; preserve its documented claim scope and tolerances.

Speed alone does not justify arithmetic/model changes or relaxed numerical tolerances, exact identities, model contracts or safety checks.

## Future performance success

`H1_REPEATABLE_SUB700MS_PERFORMANCE_PASS` may be issued only when every condition holds:

- All structural performance gates pass.
- Production acceptance timing mode is used.
- Five consecutive warmups complete.
- 100/100 consecutive measured runs complete.
- No outliers are removed.
- Every measured run is <700.000 ms.
- Maximum measured latency is <700.000 ms.
- Numerical/runtime integrity gates pass.
- Both U85 stages remain the production route.
- No CPU fallback occurs.
- Final source/build/model identities are bound.

Bind the campaign to:

- Source commit and source bundle identity.
- Built/target artifact identities.
- Model identities.
- Production route/configuration.
- Board/processor identity and clock settings.
- Timing mode and procedure.
- Frozen criteria SHA-256.

This disposition is not issued by initialization.

## Scope exclusions

This performance stage does not by itself qualify:

- Live line-input audio.
- Live microphone acquisition.
- Acoustic/biological accuracy.
- Cold-boot latency.
- USB upload throughput.
- Arbitrary boards.
- H2/H3.
- Regional head deployment.
- Shared-backbone multi-head scheduling.
- Power consumption unless separately measured.
- Energy per inference unless separately measured.

Line-input/live-microphone qualification starts only after this performance target is accepted.

## Initialization boundary

Initialization ends after two signed commits, the performance-branch push and GitHub Verified checks. One eventual PR covers the entire performance stage; there is no separate attribution PR.

Initialization starts none of the following:

- Benchmarks.
- Firmware builds/CMake/Ninja.
- Vela.
- Inference.
- Hardware access/programming/reset.
- PMU collection.
- Instrumentation changes.
- Source optimization.
- Algorithmic arithmetic changes.
- Model binary changes.
- N1 implementation cleanup.
- PR creation.
- Merge/master update.
- Tags/releases.

After initialization, the next engineering session must establish the current production diagnostic stage profile and observed baseline transfer/cache ledger before selecting optimization candidates. Acceptance timing remains a separate campaign under the frozen gate.
