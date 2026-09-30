# BirdNET V3 H1 on Alif U8 DK

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

The accepted reproduction scope covers M1–M3 TensorFlow reconstruction. Start with [REPRODUCIBILITY.md](REPRODUCIBILITY.md); compact deployment identities, development results, limitations and audio attribution are in [`records/development/`](records/development/). Accepted M1–M3 records are in [`records/m1/`](records/m1/), [`records/m2/`](records/m2/) and [`records/m3/`](records/m3/).

The current frontend comparison remains `CHARACTERIZATION_ONLY`, with `promotion = FORBIDDEN` until a justified tolerance is frozen. Existing development results are not a new physical qualification claim. The motivation and future direction described above do not change the evidence or claim boundary.

## Licensing and scope

Copyright © 2026 Taras Kuchynskyy. Original project source and firmware are Apache-2.0; original project-authored documentation is CC BY 4.0. BirdNET models and data remain under upstream terms; third-party software and external audio retain their own licenses. See [NOTICE](NOTICE), [LICENSE](LICENSE), [MODEL_LICENSES.md](MODEL_LICENSES.md), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.
