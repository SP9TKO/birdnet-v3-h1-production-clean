# BirdNET V3 H1 on Alif U8 DK

## Historical acceptance and clean public candidate

Historical M5 remains **ACCEPTED** at its original uploaded-waveform H1 scope. Its historical Matter source binding remains `MATTER_BINDING_UNRESOLVED_STOP`: the accepted workspace discovered Matter Kconfig inputs, but its exact source revision could not be bound.

The clean public candidate removes Matter prospectively through [the flat workspace manifest](firmware/h1/workspace/west.yml). It separately passed `H1_NO_MATTER_STATIC_AND_BUILD_EQUIVALENCE_PASS` and `H1_NO_MATTER_PHYSICAL_EQUIVALENCE_PASS`, reaching `NO_MATTER_PUBLIC_CANDIDATE_QUALIFIED`. This candidate has a new firmware identity; historical firmware byte equality is not claimed and historical M5 was not reaccepted.

The physical campaign covers only canonical and high_valid_two_tone with immediate repeats: four slots PASS, 12 downstream comparisons / 373,920 integer elements / 0 mismatch / 0 LSB, historical 8/8 boundaries byte-identical, repeat 8/8 boundaries byte-identical, and 8 U85 submissions / 8 completion IRQs. The historical five-fixture M5 campaign was not rerun. No biological-accuracy or arbitrary-board claim follows.

Use [the candidate authority](records/release/CANDIDATE_AUTHORITY.json), [static/build evidence](records/release/NO_MATTER_STATIC_BUILD.json), [physical evidence](records/release/NO_MATTER_PHYSICAL.json), [production setup](records/release/PRODUCTION_SETUP.json) and [candidate recreation](records/release/RECREATION.md). Wren remains a mandatory locally retained 384,000-byte build input; no public-only M5 or fresh historical executable-byte reconstruction is promised.

The author previously developed and successfully used a BirdNET 2.4 embedded implementation on NPU hardware such as the Seeed Studio Grove Vision AI Module V2 for live bird and bat acoustic recognition. That implementation remains private and is planned for a separate public release. This V3 project does not depend on it: BirdNET V3 is reconstructed independently from official public artifacts.

## Why embedded inference

That earlier work showed that live acoustic recognition can run on small embedded devices with dedicated NPUs. It motivates investigating how modern acoustic AI workloads can move from relatively expensive, memory rich and power hungry general purpose computers to low cost microcontroller class systems combining Cortex-M55 compute, DSP acceleration and dedicated NPUs.

Where a workload permits, such systems may lower hardware cost and power, reduce size, support local and offline inference, provide deterministic operation, and make it economical to deploy many sensing nodes. These are engineering advantages this project investigates, not deployment outcomes established by this repository.

## Why AI assisted engineering

This project is also an experiment in contemporary AI assisted software and hardware development. It investigates whether an individual engineer using modern AI development tools, open source software and inexpensive NPU hardware can build a complex embedded AI system with engineering discipline approaching that of a larger conventional development effort.

Speed alone is not the objective. The project examines whether AI assistance can accelerate architecture, implementation, source inspection, numerical validation, firmware development, debugging, reproducibility, evidence generation, documentation and hardware integration while preserving provenance, validation, licensing, reproducibility and explicit claim boundaries. Modern AI development tools and increasingly capable low cost NPU hardware may let an individual developer create sophisticated edge AI systems faster and at lower cost than was previously practical; this project tests that proposition with a demanding acoustic inference application.

## Inference path

```text
official BirdNET V3
→ clean H1 TensorFlow
→ M55 frontend
→ U85 backbone
→ shared feature
→ M55 GeM
→ U85 classifier
→ M55 reporting
```

The shared `[1,7,9,1280]` feature remains an explicit interface between the U85 backbone and H1 head. Firmware source is in [`firmware/h1/`](firmware/h1/).

## Status and reproduction

M1–M5 are accepted at their documented scopes. M4 binds the range-covering V3 deployment; M5 closes exact integer arithmetic, the production bridge/programmed identity and five-fixture physical V2 execution on the observed Alif E8 DK M55-HP/U85-256. Start with [PROJECT_STATUS.md](PROJECT_STATUS.md), [the cumulative M5 record](records/m5/ACCEPTED_STATE.json) and [REPRODUCIBILITY.md](REPRODUCIBILITY.md).

Native Path C frontend qualification is scoped. Host/native frontend bit identity, GeM FP32 bit identity, biological accuracy and live-microphone/performance qualification are not claimed. M5-4 V1 remains failed; V2 was frozen prospectively and accepted separately. See [M5_COMPLETION_REPORT.md](M5_COMPLETION_REPORT.md) for the exact boundary.

## Licensing and scope

Copyright © 2026 Taras Kuchynskyy. Original project source and firmware are Apache-2.0; original project-authored documentation is CC BY 4.0. BirdNET models and data remain under upstream terms; third-party software and external audio retain their own licenses. See [NOTICE](NOTICE), [LICENSE](LICENSE), [MODEL_LICENSES.md](MODEL_LICENSES.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

The original M5 documentation digest remains historical evidence in [DOCUMENTATION_IDENTITY.json](records/m5/DOCUMENTATION_IDENTITY.json). [DOCUMENTATION_SUPPLEMENT.json](records/release/DOCUMENTATION_SUPPLEMENT.json) binds the current candidate document hashes separately.
