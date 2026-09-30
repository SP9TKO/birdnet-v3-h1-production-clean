# Development policy

State: **ACCEPTED DEVELOPMENT POLICY**
Date: 2026-09-28

## Purpose

This policy governs BirdNET V3 development on Alif U8 before formal M4/M5 qualification is complete.

It exists to prevent the project from spending disproportionate time perfecting internal numerical boundaries before a real end-to-end BirdNET implementation has been demonstrated.

It does **not** change accepted model lineage, formal milestone states, or formal M4/M5 acceptance criteria.

## Required development sequence

Use this order unless a concrete blocker requires a bounded diagnostic:

```text
Phase A — functional end-to-end PoC
Phase B — real-acoustic characterization
Phase C — optimize measured bottlenecks
Phase D — architecture / optional-head optimization
Phase E — formal numerical qualification
```

### Phase A — functional end-to-end PoC

Build the smallest complete path that can turn a real or prepared 3-second waveform into BirdNET predictions on the target.

Preferred architecture:

```text
waveform
→ M55 frontend
→ U85 backbone
→ shared feature [1,7,9,1280]
   ├─ H1: M55 GeM → U85 H1 classifier
   └─ optional H23: direct H2/H3 module
→ optional fusion
→ sigmoid / reporting
```

Correctness, stable execution and observable interfaces come before performance optimization.

### Phase B — real-acoustic characterization

Use a small meaningful corpus (approximately 10–30 windows) before deep optimization.

Compare host and board at useful boundaries:

- shared feature;
- H1 embedding;
- logits;
- scores;
- top-1;
- top-5 overlap;
- top-10 overlap.

The purpose is to learn whether internal numerical differences matter to actual BirdNET behavior.

This is not a publication-quality biological accuracy benchmark.

### Phase C — optimize measured bottlenecks

Optimize only after measurement identifies a material bottleneck.

Examples:

- backbone latency → Vela/memory placement;
- M55 GeM latency → exact FP32/CMSIS-DSP/Helium implementation work;
- frontend latency → DSP/Helium optimization;
- model loading → PSRAM/residency;
- development transport → USB CDC/bulk;
- memory pressure → lifetime/arena reuse.

Do not optimize a component merely because it looks theoretically expensive.

### Phase D — architecture / optional-head optimization

Preserve the shared feature as the canonical fan-out boundary.

Required interface modes:

```text
H1_ONLY
H23_ONLY
H1_H23_FULL
```

H1:
```text
shared feature → GeM → embedding → H1 classifier
```

H2/H3:
```text
shared feature → H23 module → H2 logits + H3 logits
```

Do not route H2/H3 through GeM or the H1 embedding.

Formal H2/H3 clean-reproduction acceptance is still outside the current H1 milestone unless separately authorized.

### Phase E — formal numerical qualification

Only after the deployment architecture is selected should the project freeze and formally qualify the full stack:

- exact model/component bytes;
- TFLite/LiteRT runtime and reference/delegate mode;
- Python/runtime identity where relevant;
- Vela version and exact configuration;
- SDK and Ethos-U driver;
- compiler/linker;
- firmware;
- thread/delegate settings;
- memory layout;
- hardware target.

Formal exact/bounded conformance rules remain in force here.

## Numerical triage rule

A PoC internal mismatch does not automatically justify a large localization campaign.

Use this order:

```text
1. verify model/input bytes
2. verify tensor metadata/layout
3. verify runtime version and reference/delegate mode
4. verify compiler/Vela/driver identities
5. test deterministic repeatability
6. propagate the difference downstream
7. test real-acoustic effect
8. localize stages/operators only if materially necessary
```

Minor runtime changes may produce different integer outputs even for identical model/input bytes. Therefore deployment-runtime identity is part of the numerical contract.

Do not choose a runtime merely because it happens to be closer to hardware. Bind the intended qualified runtime prospectively.

## No early retraining

Do not retrain or fine-tune weights to compensate for hardware/runtime differences during early PoC work.

First determine whether the implementation is functionally correct and whether the difference has meaningful biological impact.

Any later training/weight change is a new model identity and requires explicit authorization, provenance and separate evaluation.

## Evidence level

Keep PoC evidence compact:

- exact component hashes;
- exact runtime/toolchain identities;
- small fixtures;
- comparison metrics;
- memory/timing summaries;
- firmware hashes;
- concise UART/board evidence.

Avoid acceptance-grade evidence forests unless the PoC is transitioning into a formal milestone.

## Relationship to formal gates

PoC labels such as `EXECUTED`, `PARTIAL`, `VIABLE` or `BLOCKED` are development classifications.

They do not create formal `ACCEPTED_STATE.json`, accept M4, authorize M5, or replace canonical TensorFlow authority.

When the PoC demonstrates a viable architecture, formal qualification starts from the selected frozen implementation rather than from every discarded intermediate experiment.
