# H1 frontend decomposition observation V1

Status: `PROSPECTIVE_FRONTEND_DECOMPOSITION_OBSERVATION_V1_FROZEN` (2026-10-08).
This is a prospective measurement freeze. Instrumentation, firmware builds,
hardware access and optimization have not started. Implementation and physical
qualification require a separate authorization. No timing here was measured by
this freeze.

The authoritative [machine-readable plan](../records/performance/FRONTEND_DECOMPOSITION_OBSERVATION_PLAN_V1.json)
has SHA-256 `623e53d0d8174ac34b0f4e54093f51ef3af980c77a49f07b806e07eb4e0b941c`. Its detailed source markers, counters, ownership,
failure rules and future qualification gates control implementation.

## Bound authority

- Entry/performance remote HEAD: `8038ca200ff9af85e824e0978689aa3784a79a56`; tree `89c3f466c2f849faf73d994a75ac4cb9d054b789`.
- Published master remains `c52270fca3f6b7fe2ccece57388bb4040590dd91`; no PR or history rewrite.
- Immutable criteria SHA-256: `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b`. Acceptance remains five warmups,
  100 consecutive runs, no exclusion, every unrounded sample `<700.000 ms`.
- Production-result contract SHA-256: `fcfab23826b80e96525693ae6819a0f396dc6c101b9b5aa58963203ad1815b73`.
- Qualified source bundle: `8e386e6e18b89540413f969249d9dd8da578cec514ff166101c24706951e8289`.
- Qualified ELF: `d56a9f1c6f1e905556d3e84bff5dfa5d40a1a07c1e119b533d86f58d8dcfabf7`; BIN: `feea14194370539ab7adacc61ca28ebf09bdc3385d5bd3bed846216323a16373`.
- Qualified map: `5662917b08517adb047165c3fb8f487fe650ce986ac53b74960a241fe282d8eb`; complete static layout:
  `13233c8c75c9ec409140199571e3549bf391a880812224d1b26c608dda3ef658`.
- Existing physical PASS receipt: `3dc37857e7210f9db489519808d0b87de856e7675c44f6d677b6788ad93d8f1c`.

The unchanged accepted authorities are lifecycle reuse `4cced1f`, top100
postprocessing `88876bc`, waveform-validation lifecycle `dee32ee`, and
`PRODUCTION_RESULT_CONTRACT_V1_IMPLEMENTATION_PASS` at the entry HEAD.
Their full hashes and the fixed model/Vela/toolchain identities are in the plan.
The entry ELF identifies the baseline, not the future instrumented artifact.

The existing qualified 5+100 primary latency is mean **1242.221984275 ms**,
minimum 1241.622555, median 1241.8894225, nearest-rank p95 1243.77543,
maximum 1244.145825 and population SD 0.7796879464612986 ms. All 100 are
at least 700 ms; none passes. The realized prior saving was 482.114302525 ms.
The existing observed-production 5+20 mean is 1242.623005125 ms.

| Existing disjoint component | Prior mean ms |
| --- | ---: |
| Frontend caller envelope | 697.5501925 |
| Backbone invoke | 221.065402375 |
| GeM | 118.008296375 |
| Other residual | 94.5905505 |
| Classifier invoke | 91.631053 |
| Score generation/finite check | 10.091248875 |
| Top100 selection | 9.683755625 |
| Compact publication | 0.002505875 |

These are retained authorities, not frontend substage measurements. In particular,
CRC cost and frontend compute without CRC remain unknown.
`RESULT_EVIDENCE_ZERO_SCAN_SUBGATE_PASS`, `ZERO_SCAN_TARGET_NOT_MET` and
`POST_PRODUCTION_RESULT_SUB700_TARGET_NOT_MET` are preserved.

## Executed source route and geometry

`runResultCommand(diagnostic=false) -> runOnce(Production, legacyReference=false)
-> h1RunFrontend() -> h1RunFrontendM55(false,false)` is the measured route.
Use the exact committed source hashes and entry line ranges in the plan.

The input is 96,000 FP32 samples/384,000 bytes at 32 kHz. A 2048 real FFT,
512 hop and 1025 spectrum bins produce 188 frames. Each consecutive four
frames is followed by one compact mel group: 47 groups, 128 bands,
1,885 exact coefficients (2,048 reserved). Crop is 125x188, gray resize is
224x281, and pixel-major output is 224x281x3 = 188,832 FP32 elements /
755,328 bytes. All dimensions were checked against source and the embedded
qualified frontend model; a mismatch is a hard stop.

Production uses `stopAfterStage=POWER_SQUARES(7)`, `computeScalarPower=true`,
`useCmsisPower=false`. The selected power loop is scalar `real*real+imag*imag`,
with zero imaginary DC/Nyquist. It does not run hypot or CMSIS power branches.
Hann uses `arm_mult_f32`, packed 1024 complex forward FFT uses `arm_cfft_f32`,
and the project real split reconstructs the 2048 real transform in place.

Compact mel uses MVE floating-point gather of four frame lanes, one broadcast
coefficient and separate vector multiply/add in increasing coefficient order.
The exact compiled function proves this path and `usedMve=true`. The scalar
fallback is compiled out. `captureStages=false`: all 188 per-lane finite
checks execute, and no melRaw capture copies execute. The operation map also
includes all setup, readiness, null diagnostic dispatch and status bookkeeping.
Boot-only constant validation/table and CMSIS construction stay outside the
steady-state route; per-inference checks remain timed.

## Timestamp partition

All values are absolute uint64 cycles with explicit presence bits. Use the
unchanged 400 MHz `k_cycle_get_64`/`h1ProfileNow` 64-bit SysTick extension,
10 kHz tick configuration. Capture is bounded O(1); existing timer locking
and variable physical read latency must be disclosed. No new timebase,
clock or cache policy is introduced.

| ID | Executed interval | Entry source lines | Calls |
| --- | --- | --- | ---: |
| F0 | Complete selected frontend wrapper call | [958, 963] | 1 |
| F1 | Wrapper entry, native argument/readiness checks and setup | [492, 528] | 1 |
| F2 | All spectral group envelopes | [531, 555] | 47 |
| F3 | All mel group envelopes | [556, 577] | 47 |
| F4 | Log/dB, global maximum and floor bookkeeping | [581, 594] | 1 |
| F5 | Crop, clamp, fused min/max and normalization | [596, 614] | 1 |
| F6 | Half-pixel resize envelope | [616, 620] | 1 |
| F7 | Three-channel FP32 layout and normalization | [622, 633] | 1 |
| F8 | Full frontend output finite scan | [636, 638] | 1 |
| F9 | Frontend internal full-output CRC | [639, 640] | 1 |
| F10 | Native status/exit and wrapper return | [641, 672] | 1 |
| F_RESIDUAL | Uncovered frontend gaps and observer bookkeeping | gap complement | derived |

F0 brackets the actual selected wrapper expression in `main.cpp:958-963`.
The older frontend envelope starts at 951 and ends at 963 and includes the
caller prelude. Preserve both: `F_PROFILE = caller-pre + F0 + caller-post`.
The previous 697.5501925 ms belongs to F_PROFILE. No older timer is moved.

F2 and F3 alternate 47 times. Each spectral frame retains the existing raw
pairs for reflected preparation (290-295), Hann (309-311), CFFT (325-327),
real split (338-342) and selected power (379-390) in
`frontend_m55_spectral.cpp`. New call brackets expose setup/return gaps.
S6_FRAME is each call's independently summed uncovered gaps; S6_GROUP is
the group gaps outside four calls. Their sum is S6. Counts are exactly 188
for each selected kernel; unselected power/capture/diagnostic counts are zero.
`transformCycles` is CFFT+split and `totalCycles` is a kernel sum: neither is
an additional disjoint component or a full frame call.

Mel retains G_MEL, M_CALL, M_MATH and four M_FINITE pairs per group.
M_MATH starts after the guards at 450 and ends after stores at 472.
M_FINITE brackets 564-569 for each lane. Independently retain function
setup/usedMve/return gaps and group finite/capture-branch/timing bookkeeping
gaps. Conditional capture has count/bytes zero and no fabricated timestamps.
No change to FP32 accumulation order is permitted.

F4 retains a DB_MATH child with `logf(max-via-strict-comparison(sum,epsilon))`
and the existing log10/dB multipliers, global maximum and 100 dB floor.
F5 separates the crop/clamp/fused min-max loop (598-609) from denominator
and `1-((value-min)/((max-min)+epsilon))` normalization (610-612).
F6 retains the exact half-pixel floor/ceil/clamp four-product interpolation
in `resizeHalfPixel:256-279`. F7 retains the three channel means/inverse
standard deviations and all original expression order. Parent/child gaps
are explicit and never added twice.

Existing native `timing.totalCycles` runs from 524 to 634. It excludes
wrapper/entry checks, final output validation, CRC and return. Retain its
raw boundaries separately from the pre-validation call envelope, final
scan, CRC, exit and complete F0. The ordinary wrapper discards its local
timing structure; the observer retains metadata only.

F8 directly brackets `finiteCount=0` and all 188,832 `isfinite` tests at
636-638. F9 directly brackets the existing assignment/call at 639-640 to
the local bitwise CRC32 (59-69). Retain `FRONTEND_INTERNAL_CRC_CALLS=1`,
`FRONTEND_INTERNAL_CRC_BYTES=755328`, cycles `CRC_END-CRC_BEGIN`, and ms
`1000*cycles/qualified_clock_hz`. The CRC remains after the scan and before
the existing finite-count failure decision. Preserve this order on errors.
Do not substitute older removed-CRC measurements, estimate throughput,
remove/bypass the CRC or move it outside primary time.

## Logical byte ledger

Values count logical array accesses across one successful inference,
including repeated accesses. They are not measured bus traffic or cache
misses. Register/scalar bookkeeping and span metadata are distinguished.

| Operation | Elements per call | Calls | Logical reads bytes | Logical writes bytes |
| --- | ---: | ---: | ---: | ---: |
| S1 | 2048 | 188 | 1540096 | 1540096 |
| S2 | 2048 | 188 | 3080192 | 1540096 |
| S3 | 1024 | 188 | unresolved multi-pass traffic | unresolved multi-pass traffic |
| S4 | 1025 | 188 | 3080192 | 1540096 |
| S5 | 1025 | 188 | 1540096 | 770800 |
| M_MATH | 512 | 47 | 1771900 | 96256 |
| M_FINITE | 128 | 188 | 96256 | 0 |
| M_CAPTURE | 128 | 0 | 0 | 0 |
| DB_MATH | 24064 | 1 | 96256 | 96256 |
| CROP_CLAMP_MINMAX | 23500 | 1 | 94000 | 94000 |
| GLOBAL_NORMALIZE | 23500 | 1 | 94000 | 94000 |
| RESIZE_MATH | 62944 | 1 | 1007104 | 251776 |
| LAYOUT_MATH | 188832 | 1 | 755328 | 755328 |
| F8 | 188832 | 1 | 755328 | 0 |
| F9 | 755328 | 1 | 755328 | 4 |

Real split reads include source/twiddle expressions; compiler alias
coalescing is not claimed as physical traffic. MVE mel math reads four
power values and one coefficient per coefficient/group. Layout reads
three gray expressions per pixel, which the compiler may hoist. CFFT
multi-pass/twiddle traffic is deliberately unresolved, with its known
8192-byte workspace extent retained. No guessed byte count fills a gap.

## Cycle reconciliation and production primary

For every record, in integer cycles:

`F0 = F1 + sum(F2) + sum(F3) + F4+F5+F6+F7+F8+F9+F10 + F_RESIDUAL`.

Independently derive every uncovered gap from ordered retained endpoints
before, between and after components. Require gap sum equal to the
container duration minus disjoint components. Perform the same proof
inside each spectral group/call, mel group/call and post-mel parent.
Reject missing/duplicate markers, negative/reversed intervals, wrong
clock, overlap, incorrect counts, overflow or any non-reconciling sum.
Equal shared endpoints and zero-length uncovered gaps are valid.

Production P0 is the existing `proof.primaryStart` at `main.cpp:3213`.
P1 is the existing `proof.ready=proof.publicationEnd` at 3254-3255,
after complete `service.publish`: compact slot copy, release barrier,
`productionReady=true`, function return. Preserve this exact observable
endpoint and the waveform-ready -> complete PRODUCTION_RESULT_READY rule.

The primary partition is entry gap, frontend envelope, frontend/backbone
handoff, backbone invoke, backbone/GeM handoff, GeM, GeM/classifier
handoff, classifier invoke, classifier/score handoff, score/finite,
score/top gap, top100 selection, top/publication gap and publication.
Their ordered endpoints telescope exactly to P1-P0. Quantization,
bridge, NPU cache/IRQ and child frontend timings stay nested in their
parents. No interval is added to a containing parent. Postprocessing
materialization/safety/return bookkeeping remains in named gaps.

Baseline commands 144/145/146/150 use legacy diagnostic result/save/CRCs
and are not production measurement authorities. Diagnostic163 and
complete-frontend37 are also unsuitable substitutes. Keep production
bypasses of seven result CRCs, diagnostic repeat/full save/top100
publication and diagnostic profile finalization intact.

## Memory ownership and prospective placement

The exact map/compiled structures were audited before proposing storage.
Both model contexts, model guards, arenas, boundary workspaces, current
result, diagnostic snapshots and hot-path observer reservations remain
owned. The complete existing inventory is bound by its hash above.

| Existing object/reservation | Address | Bytes |
| --- | --- | ---: |
| PSRAM.model | 0xa0000000 | 20465840 |
| PSRAM.modelGuard | 0xa13848c0 | 64 |
| PSRAM.current | 0xa1384900 | 1371328 |
| PSRAM.first | 0xa14d35c0 | 1371328 |
| PSRAM.frontendScratch | 0xa1622280 | 624384 |
| PSRAM.spectralDiagnostics | 0xa16ba980 | 24768 |
| PSRAM.uploadPayload | 0xa16c0a40 | 384060 |
| PSRAM.uploadedWaveform | 0xa171e680 | 384000 |
| PSRAM.classifierModel | 0xa177c280 | 13008016 |
| PSRAM.classifierModelGuard | 0xa23e3f20 | 64 |
| _ZN12_GLOBAL__N_111gLastResultE | 0x20001a20 | 1880 |
| _ZN12_GLOBAL__N_112h1CompactMelE | 0x2400000 | 8968 |
| _ZN12_GLOBAL__N_119gM55FrontendRuntimeE | 0x20002798 | 108 |
| _ZN12_GLOBAL__N_119h1SpectralWorkspaceE | 0x2402320 | 8192 |
| h1BaselineState | 0x2500000 | 60704 |
| h1FastMemory | 0x2356b00 | 524288 |
| h1PersistentStages | 0x200010c0 | 1216 |
| h1PostprocessObserverState | 0x2510000 | 2696 |
| h1ProductionRuntime | 0x2514000 | 592 |
| h1TensorArena | 0x2000000 | 3500800 |
| h1ValidationObserverState | 0x2511000 | 7240 |
| h1WaveformLifecycle | 0x2513000 | 1568 |
| z_interrupt_stacks | 0x20003fb8 | 2048 |
| z_main_stack | 0x200048f8 | 32768 |
| backbone_arena | 0x02000000 | 3444832 |
| backbone_arena_guard | 0x2349060 | 64 |
| classifier_arena | 0x023490a0 | 55840 |
| classifier_arena_guard | 0x2356ac0 | 64 |
| fast_guard | 0x23d50e0 | 64 |

`scratch.gray` first 4100 floats is groupPower until all mel groups end;
resize subsequently writes the full gray image. melDb is transformed in
place, and image is normalized in place. No scratch reuse is optimized.
Unused ordinary-route legacy/diagnostic buffers remain reserved. Static
layout does not establish stack peak or peak live memory.

Prospective dedicated NOLOAD SRAM1 observer region is
**[0x02520000,0x025f0000)**, 851,968 bytes, 32-byte alignment. SRAM1 is
[0x02400000,0x02800000); the prior last reserved region ends at 0x02515000.
No existing object or reservation overlaps the proposed region.
This is a layout plan, not a linker edit or physical storage claim.

The budget is two 32-byte guards + 4096-byte header + 25 fixed 32,768-byte
records = **823,360 bytes**, with 28,608 bytes reserved slack. Each record
has 512-byte metadata/counters, 64 fixed u64 timestamps, 188x12 frame
u64 timestamps, 47x16 group u64 timestamps and 3072 presence bits:
25,472 bytes payload, 7296 bytes stride padding. Five warmups and twenty
measurements retain distinct records. No heap; observer stack increment
must stay at most 256 bytes. New compiled ABI/layout, guards/accessibility
and every unchanged old address/size must pass future qualification.

Initialize records before warmups, write raw metadata directly into fixed
slots, retain them until explicit collection/release, and retrieve only
when stopped. Do not place a sample on stack, copy intermediate tensors,
reclaim diagnostic capacity, move production buffers or add cache flushes.

## Future campaign, overhead and qualification

All 65 entry request allocations were audited. Prospective observer
commands **168/169/170** are collision-free: run autonomous campaign,
get bounded status, get bounded retained record range. They are not
implemented or allocated in firmware by this freeze. Reaudit all
allocations before implementation; 160-167 and every diagnostic opcode
remain reserved. Authenticated fixed request layouts and bounded range
readout are specified in the plan; framing/result schemas stay unchanged.

Share only the unchanged ordinary161 production admission/compute/publish
core with the future campaign. The new command runs **5 warmups + 20
consecutive measured production inferences** on one exact programmed
observer binary and one validated resident upload generation. No uploads,
diagnostic inference, per-run output or host commands interrupt the loop.
All raw timestamps, counters, admitted tokens and errors are retained.
Records are fetched after completion and the complete set is immediately
qualified and sealed. Pre-campaign qualification of nonexistent records
is not required. Any record failure preserves all records and stops:
no exclusion, selective retry or automatic repeat campaign.

Reuse the existing kernel reads; new call/group/validation pairs and fixed
markers add at most **1192 clock reads per run**. Pair/counter stores follow
end reads. No heap, per-frame USB/printf, new tensor CRC, intermediate
copy, arithmetic/configuration change or buffer relocation is allowed.
Actual captures/stores/bytes, emitted hook instructions and separately
qualified bounded read/store calibration must disclose overhead. All
overhead remains in recorded components/gaps; no guessed or calibrated
constant is subtracted. Residual also contains original uncovered work,
so it is not a pure observer-overhead estimate. Host-only checks cannot
prove negligible physical interference.

For each component retain all twenty raw integer cycle values, exact sum,
actual clock, min/mean/median/p95/max/population SD, calls and measurable
logical bytes. Nearest-rank p95 is the **19th sorted sample of 20**;
population SD divides by20. Convert cycles to ms only after exact
reconciliation; no rounding/removal before qualification.

Physical numerical qualification uses canonical, immediate canonical
repeat, high_valid_two_tone and immediate two-tone repeat. Require exact
waveform, full frontend bits, all integer boundaries, GeM embedding,
all11,560 score bits, accepted top100, production top3 bits/indices,
counts, input identities and diagnostic CRCs. All integer/LSB/frontend/
score/top100/top3/boundary-CRC mismatch gates are zero. Capture complete
production buffers read-only after isolated production Ready with proven
writer stability; do not weaken evidence nonretention or add product
scans. Exercise unchanged diagnostic qualification separately, outside
the autonomous production campaign. No numerical tolerance changes.

Also require identical operation counts/backends, model/Vela identities,
dimensions, U85 execution/lifecycle, waveform generation, Ready/diagnostic
semantics, memory ownership/cache policy and result schema; zero fallback,
faults, guards, resets, drift, mutations and unintended evidence scans.
Observer structural PASS, numerical PASS, physical runtime PASS and
measurement qualification are separate from optimization/performance or
formal milestone acceptance. The future binary/readback needs its own
exact identity. No qualification is claimed by this freeze.

## Mandatory next gate and stop

`FRONTEND_OPTIMIZATION_REQUIRES_TINY_BIRDNET_RECOMPARISON=true` is a project
gate. After separately implemented, physically qualified 5+20 decomposition,
the **immediately next task must be TINY_BIRDNET_M55_RECOMPARISON_V1**.
Only after it completes may quantified opportunities be reviewed and one
prospective H1 frontend candidate be selected/frozen; implementation then
needs separate authorization. A blocked comparison blocks candidate freeze.

The future comparison must bind one authoritative repository/private
locator, remote, branch, commit/tree, dirty/untracked/source hashes,
build/toolchain/flags/CPU/clock/FPU/MVE/cache, placement, libraries and actual
benchmark/memory evidence. Compare algorithm, FFT/mel/loops, FP semantics,
MVE/CMSIS use, dimensions, boundaries/cycles, SRAM/PSRAM/flash/RO/stack and
peak scratch/live memory where available. Establish whether a difference
comes from acceleration, layout/reuse, algorithm/dimensions/semantics,
omitted work, timing boundaries, hardware/toolchain or another evidenced
cause. Do not infer interchangeability from performance. Multiple possible
reference states require a stop and authoritative identity resolution.
Inspection does not authorize copying: code/tables/dependencies need
separate provenance/license review and implementation/reuse authorization.
No tiny-birdnet source was inspected in this freeze.

Hard stops include authority/evidence/geometry mismatch, ambiguous route,
non-reconciling timestamps, overlapping observer storage, functional change
needed to time CRC, changed primary/numerical/cache/clock/compiler/algorithm,
diagnostic work substituted into production, or weakened tiny comparison
ordering/provenance gate. Do not expand scope to rescue a failure.

The future files expected to need measurement-only work are frontend.cpp,
frontend_m55_spectral.cpp, main.cpp, usb_transport.hpp/.cpp, CMake linkage/
identity and new bounded frontend observer header/source/linker files.
Existing model storage, numerical kernels, cache policy and compiler flags
have no authorized semantic change. Every future diff must be classified.

Freeze dispositions are source/timer/byte/memory audits complete, schema
defined, separate CRC measurement defined, noninterference gates defined,
scope PASS, mandatory tiny gate preserved, and observation V1 frozen.
`OBSERVER_IMPLEMENTATION_NOT_STARTED` and
`TINY_BIRDNET_RECOMPARISON_NOT_STARTED` remain true. Commit only this
companion and the plan, SSH-sign with the required fingerprint, verify
locally GOOD and GitHub Verified, push performance only, then stop.

**Stop before frontend observer implementation.** The exact next action is
a separately authorized implementation and physical qualification of this
frozen observer, followed immediately by the mandatory tiny comparison.
