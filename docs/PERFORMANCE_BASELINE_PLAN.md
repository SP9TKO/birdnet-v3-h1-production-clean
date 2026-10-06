# H1 unchanged-production baseline plan V1

This prospective plan is frozen before current H1 timing or PMU observation. The authoritative [measurement plan](../records/performance/BASELINE_MEASUREMENT_PLAN_V1.json) and [PMU plan](../records/performance/BASELINE_PMU_PLAN_V1.json) become immutable once signed, pushed and GitHub Verified. A technical defect requires a prospectively frozen V2 before more measurement.

| Binding | Identity |
| --- | --- |
| Date | 2026-10-06 |
| Branch/base HEAD | `performance/h1-sub700ms` / `bea3bd9b6f7858dd8a249e92a74f0cdb76d433c3` |
| Published master | `c52270fca3f6b7fe2ccece57388bb4040590dd91` |
| Frozen criteria SHA-256 | `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b` |
| Plan SHA-256 | `a5fffa5aff60ca7bad09842a1b6c0ba13d2921c85d2e492f29c01704663b0bdc` |
| PMU plan SHA-256 | `3a1f8e5e20d22410ea5527cb818d38a08eb2454eec533245a8328ff28558b218` |
| Canonical waveform | float32 little-endian, 32 kHz mono, 96,000 elements, 384,000 bytes; SHA-256 `23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8` |
| Hardware | Alif E8 DK, M55-HP, Ethos-U85-256 |
| Backbone | `0xc0000000`, 20,465,840 bytes; SHA-256 `f3328db0aa2d7befc13d0f3dd7aa20906ba2164d15f589ddb1f746de398fc2e7` |
| Classifier | `0xc2000000`, 13,008,016 bytes; SHA-256 `2ff002cb4bf95b33384d70023a34e377b1d34bb2786e39efc5d358c3f80a6e93` |

The user authorized unchanged-baseline characterization with existing structural failures recorded. Each ordinary inference stages 33,473,856 model bytes into PSRAM and constructs two transient interpreters. Include these copies and existing CRC/validation/allocation costs. The frozen no-per-run-copy/reconstruction gates remain failed; this characterization cannot issue `H1_REPEATABLE_SUB700MS_PERFORMANCE_PASS`. Preserve historical M4/M5/release evidence.

Start with the valid canonical waveform resident in the production uploaded-waveform buffer and the request ready, before ordinary setup and waveform validation. End after the ordinary result is published and ResultReady recorded, before transport serialization/output. Include all existing per-run work, required cache, both U85 stages, frontend/quantization, shared-feature/GeM ownership, exact bridge, sigmoid/ranking, ordinary integrity/report metadata and publication. Exclude upload/acquisition, startup, genuine one-time initialization and external diagnostic export. No existing work may be removed or moved out for a faster baseline.

Run one exact image in this order: preflight and optional one non-measured smoke; diagnostic five warmups plus 20 measured runs; PMU group1 five plus 20; acceptance five plus 100. All runs are consecutive from the resident waveform, with no per-run host command/output and no outlier removal. Export RAM records after each complete campaign. Acceptance adds only monotonic timing/minimal state and one raw sample; no added PMU configuration, tensor sweeps, debug copies or transport inside its interval. Existing ordinary checks remain. Diagnostic totals cannot replace acceptance totals.

Retain all 12 required stages with the JSON formulas: spectral groups, compact mel, frontend remainder/quantization, backbone preparation, backbone command-to-IRQ, backbone-to-GeM completion, GeM, exact bridge, classifier preparation, classifier command-to-IRQ, ordinary result completion/publication, and primary total. Report an explicit setup/reconciliation residual including waveform CRC and unassigned bookkeeping. Preserve separate model lifecycle subtimings/counts.

One frozen group uses seven of U85's eight event counters: NPU active (35), MAC active (48), WD active (80), WD stalled (81), external read-request stall (387), SRAM read-request stall (131), SRAM write-request stall (136). The separate SRAM write event prospectively completes SRAM contention characterization. Disable counter7. Retain the 48-bit cycle counter, raw pre-command/earliest-IRQ snapshots and overflows separately for backbone/classifier. No groups may be added after observation. Per-stage event/cycle ratios do not establish hypothetical milliseconds saved.

Use `k_cycle_get_64` and the pinned 64-bit SysTick extension under its spinlock. Fresh configuration must confirm the 400,000,000 Hz basis, 10,000 Hz tick rate and 64-bit symbols; do not infer frequency from historical timing. Verify runtime monotonic ordering and SysTick clock source. Preserve interrupts so the 24-bit hardware counter is extended correctly; prevent multiple unobserved wraps. Compare raw cycles using exact arithmetic, `cycles * 1000 < 700 * clock_hz`. Report count, min, mean, median, nearest-rank p95, max, population SD and both strict-threshold counts.

The transfer ledger combines current source, new ELF/map, actual addresses/sizes/cacheability/lifetimes, diagnostic timing and unchanged cache-call observations. Record actual cache ordering relative to IRQ because the pinned wait invalidation loop precedes blocking semaphore wait. Whole-cache bytes mean actual cache capacity, not scratch request length; retain both raw and rounded range bytes. Distinguish CPU copies, conversion writes, logical frontend passes and U85 internal traffic.

Instrumentation is observation/control only. Bound a separate NOLOAD SRAM1 section at `0x02500000` to 65,536 bytes; initialize it outside intervals and prove nonoverlap. Existing production buffer addresses, arithmetic, ownership, caches, optimization flags, clocks and model/Vela identities must remain unchanged. Audit source before build and exact memory layout afterward.

Use the [qualified no-Matter production setup](../records/release/PRODUCTION_SETUP.json), flat pinned manifest, SDK 0.17.0/GCC12.2.0, unchanged NONE audio/ON frontend diagnostics and existing floating-point/optimization settings. Build privately with all five exact payload gates. Do not run Vela or regenerate models.

Use the hash-bound qualified application/ATOC generator/writer. Program only H1-HP and required DEVICE/certificate/ATOC with no tool reset; verify both physical model hashes before/after and exact application/ATOC readbacks. Capture useful live state first. Confirm SW4=SE for programming, then U4, prepare continuous UART/Device USB capture and separately request a physical full power cycle. Never infer switch state or cold boot.

Stop on identity/ref/criteria mismatch, missing signed/Verified prospective commit, semantic instrumentation or layout regression, persistent model write or unexpected lifecycle/identity change, fallback, invalid timer/PMU/overflow, missing records, fault/reset/guard failure or defective instrumentation. Above-target latency requires continued characterization. After first current observation, source edits/candidate experiments are forbidden. Keep measurements/raw results private; no PR/merge/master change. Rank evidence, recommend at most one future candidate, and stop before implementation.
