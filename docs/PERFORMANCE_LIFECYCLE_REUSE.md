# H1 lifecycle reuse candidate V1

The coherent V2 baseline confirms per-inference staging of 33,473,856 model bytes and two interpreter constructions, allocations and model binds. Its acceptance-format mean is 38197.380646 ms; 100 samples were retained. The baseline latency observation is `BASELINE_SUB700_TARGET_NOT_MET`. Diagnostic lifecycle/handoff preparation accounts for 93.798461% of diagnostic total. The dominant work is repeated model CRC, exact-byte comparison and staging, rather than the small interpreter/AllocateTensors brackets.

The authoritative [candidate definition](../records/performance/LIFECYCLE_REUSE_CANDIDATE_V1.json) freezes **LIFECYCLE_REUSE_ONLY** as the first candidate for a separately authorized implementation session. This commit contains contract/documentation only. It does not implement or accept lifecycle reuse and does not issue overall sub-700-ms acceptance.

## Initialization and persistence

For backbone and classifier, perform exact model staging, initial identity/FlatBuffer/model/tensor validation, model binding, resolver/context construction, MicroInterpreter construction, AllocateTensors/custom-op preparation and input/output lookup once. Retain the valid prepared contexts; each new inference invokes them without payload copies, model binds, reconstruction or AllocateTensors.

The baseline uses one shared PSRAM model slot and one whole SRAM0 arena. Persisting both aliased views is unsafe. Necessary lifetime-only storage isolation is part of this contract: retain disjoint immutable stage payload spans within the existing PSRAM execution region and disjoint allocator-owned head/tail/scratch/temp spans within the unchanged SRAM0 parent reservation. Additional stage storage serves context lifetime only. Preserve all production checkpoint buffers, ordinary stage copies and their ownership. Keep the existing 0x02356b00/524,288-byte fast reservation and cacheability unchanged.

Fresh complete static memory, allocator preparation-peak, ownership, bounds and guard qualification is mandatory before physical reuse. Current steady arena use is not peak proof. If safe isolation cannot be qualified without an excluded change, stop this candidate. This definition qualifies no new layout or runtime addresses.

## Each waveform

Overwrite the complete existing backbone/classifier input tensors (377,664/2,560 bytes). Preserve NPU completion, cache operations, guards, output copies (161,280/23,120 bytes), GeM/bridge, postprocessing, ordinary CRCs and result publication. Keep stage boundaries unchanged.

The exact models have zero variable tensors; the sole Ethos-U op has no reset callback. TFLM manages scratch handles and resets temporary allocator chains after each node. Eval rewrites active base addresses/sizes, reserves the driver, synchronously invokes/waits, returns the job to idle and releases the reservation. Do not persist a borrowed driver handle or invent blanket scratch/core resets. Reset profile/run state per waveform using existing semantics; keep initialization counters distinct from measured operations. Do not clear faults, rewrite guards or recreate contexts to hide a failure.

## Required future proof

Every measured inference has zero model payload copy bytes/operations, interpreter constructions, AllocateTensors calls and model binds. Model/interpreter/arena/input/output addresses remain stable; exact model/Vela identities, production mathematics and both U85 routes are unchanged. CPU fallback, persistent mutation, unexpected resets, faults and guard failures are zero.

Use the already-qualified clean-candidate subset: canonical and its immediate repeat, then high_valid_two_tone and its immediate repeat. Require exact native quantized backbone input, backbone output, classifier input and classifier output against the bound qualified authority: TOTAL_INTEGER_MISMATCHES=0 and MAXIMUM_LSB_ERROR=0. Capture applicable embedding-bridge evidence and preserve existing GeM FP32 characterization scope; introduce no floating tolerance. Honor any stronger existing prospective authority.

The same reused contexts must survive all five warmups and 100 measured timing runs without recreation, pointer drift or queue/IRQ failure. Keep the same frozen waveform-ready → ordinary result-ready boundary, minimal timing mode, no outlier removal and strict unrounded sample <700.000 ms. `LIFECYCLE_REUSE_ONLY_PASS` and `H1_REPEATABLE_SUB700MS_PERFORMANCE_PASS` are separate: correct lifecycle reuse may pass implementation/equivalence while the overall latency milestone remains pending.

## Exclusions and evidence

This candidate excludes other copy removal, stage-boundary alias redesign, cache reduction/range/policy optimization, U85 staging or fast-memory optimization, faster-region relocation, model/Vela changes, frontend fusion/arithmetic, GeM or sigmoid/top-k optimization, clocks/compiler flags and DMA. Any such requirement, unresolved mutable lifetime or failed memory qualification stops the lifecycle-only candidate.

The JSON binds unchanged criteria `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b`, V2 plan `c650393e504200c4d1e637b32019978ed48846a2`, qualified instrumentation `f5411cfef6e66c71c5e9f8934bb58d9779ef157d`, exact coherent firmware, acceptance/lifecycle/cache/transfer/PMU results, the state audit and immutable baseline receipt SHA-256 `3304efe7a000b76fcd2b5ec43d2cd62a2acf809a1ac7645847cd1bd979c6b819`. Raw/private evidence and restricted payloads are not embedded.

**Stop before implementation.**
