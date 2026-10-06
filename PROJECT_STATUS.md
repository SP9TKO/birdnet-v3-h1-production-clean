# Project status

## Historical acceptance and clean public candidate

Historical M5 remains **ACCEPTED** at its original uploaded-waveform H1 scope. Its historical Matter source binding remains `MATTER_BINDING_UNRESOLVED_STOP`: the accepted workspace discovered Matter Kconfig inputs, but its exact source revision could not be bound.

The clean public candidate removes Matter prospectively through [the flat workspace manifest](firmware/h1/workspace/west.yml). It separately passed `H1_NO_MATTER_STATIC_AND_BUILD_EQUIVALENCE_PASS` and `H1_NO_MATTER_PHYSICAL_EQUIVALENCE_PASS`, reaching `NO_MATTER_PUBLIC_CANDIDATE_QUALIFIED`. This candidate has a new firmware identity; historical firmware byte equality is not claimed and historical M5 was not reaccepted.

The physical campaign covers only canonical and high_valid_two_tone with immediate repeats: four slots PASS, 12 downstream comparisons / 373,920 integer elements / 0 mismatch / 0 LSB, historical 8/8 boundaries byte-identical, repeat 8/8 boundaries byte-identical, and 8 U85 submissions / 8 completion IRQs. The historical five-fixture M5 campaign was not rerun. No biological-accuracy or arbitrary-board claim follows.

Use [the candidate authority](records/release/CANDIDATE_AUTHORITY.json), [static/build evidence](records/release/NO_MATTER_STATIC_BUILD.json), [physical evidence](records/release/NO_MATTER_PHYSICAL.json), [production setup](records/release/PRODUCTION_SETUP.json) and [candidate recreation](records/release/RECREATION.md). Wren remains a mandatory locally retained 384,000-byte build input; no public-only M5 or fresh historical executable-byte reconstruction is promised.

Historical milestone reconciliation: 2026-10-05. Candidate qualification: 2026-10-06. Project: **BirdNET V3 H1 Clean Reproduction and Live Inference on Alif U8 DK**. Observed physical qualification target: Alif E8 DK M55-HP / Ethos-U85-256.

| Milestone | State | Current meaning |
|---|---|---|
| M1 | **ACCEPTED** | Official TensorFlow FP32 H1 authority |
| M2 | **ACCEPTED** | Clean in-memory TensorFlow FP32 H1 |
| M3 | **ACCEPTED** | Serialized/reloaded canonical H1, tree `4321d40230ba518912a7629ecc11cede7cf3c917e3ab4f8331b04e769b6dd22a` |
| M4 | **ACCEPTED** | Range-covering V3 componentized deployment at scoped frontend/host semantics |
| M5 | **ACCEPTED** | Exact arithmetic → exact production bridge/programmed identity → prospective physical V2 → compact cumulative recreation closure |

M5-2: 38/38 fixtures, 152/152 comparisons, 10,727,856 integer elements, 0 mismatches/0 LSB; 8/8 repeat captures byte-identical. M5-3: 271,887 bridge integer comparisons exact, 16 active stale bindings corrected, production build/linked route and complete source→build→programmed chain PASS. M5-4 V2: 5/5 primary fixtures and 7/7 slots, 15 primary/21 total downstream comparisons, 467,400 primary/654,360 total downstream elements exact, 14 U85 submissions/14 completion IRQs, 8/8 repeat captures, scoped memory/integrity PASS.

M5-4 V1 stays permanently NOT ACCEPTED. Native frontend scope does not assert host/native bit equality; GeM FP32 residual stays characterization, while classifier-input INT16 is exact. No new V3 biological semantics, live microphone, final performance/resource or arbitrary-board qualification is claimed.

The accepted model lineage is official TensorFlow FP32 → canonical H1 → accepted V3 TensorFlow Lite → pinned Vela/M55 → observed Alif E8 target. H1 scores use the 11,560 official labels and are distinct from released fused full-predictor predictions.

Primary records: [accepted-state index](ACCEPTED_STATE_INDEX.md), [M4 binding](records/m4/ACCEPTED_STATE.json), [M5 cumulative state](records/m5/ACCEPTED_STATE.json), [M5 completion](M5_COMPLETION_REPORT.md), [recreation](records/m5/RECREATION.md). Historical `records/development/` records retain their original scope. Repository/public binary publication, microphone qualification and performance/resource proof remain separate actions.

The original M5 documentation digest remains historical evidence in [DOCUMENTATION_IDENTITY.json](records/m5/DOCUMENTATION_IDENTITY.json). [DOCUMENTATION_SUPPLEMENT.json](records/release/DOCUMENTATION_SUPPLEMENT.json) binds the current candidate document hashes separately.
