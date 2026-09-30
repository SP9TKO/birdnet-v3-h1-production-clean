# Evidence Retention Policy

State: **ACCEPTED project policy**.

The Git repository stores only compact, cumulative, decision-relevant evidence and the source needed to recreate accepted artifacts. Scratch data, downloads, caches, duplicate executions, verbose dumps and intermediate conversion/build products are not normal Git payloads.

## Retain in Git

For each accepted milestone retain only what is required to understand, verify and continue from the accepted state:

- milestone definition and completion report;
- cumulative `records/mN/ACCEPTED_STATE.json`;
- immutable source/model identities: URL, byte size, SHA-256 and published checksum where applicable;
- exact dependency/runtime lock plus package/wheel hashes;
- compact topology/semantic summaries and required constants;
- compact numerical metrics, worst-case witnesses and repeatability/equivalence results;
- one canonical fixture/reference set when later milestones require exact bytes;
- source code/configuration required to recreate the accepted artifact;
- human-readable reproduction instructions;
- project status/decision/paper updates.

## Do not retain in Git

Do not commit:

- downloaded official model payloads merely because they were inputs;
- duplicate copies of immutable upstream objects;
- wheel caches or virtual environments;
- temporary checkout/work directories;
- duplicate run outputs once repeatability is proven;
- full tensor/graph dumps when compact identities and selected witnesses suffice;
- temporary calibration banks or decoded media;
- compiler/build scratch trees;
- failed-run payloads when a compact failure record is sufficient;
- logs that merely repeat cumulative records;
- timestamped evidence trees for every experiment.

## Anti-sprawl rule

Accepted `main` uses one cumulative evidence namespace per milestone: `records/m1/` through `records/m5/`.

Working branches may contain diagnostic evidence temporarily, but milestone closure must consolidate it.

A proposed accepted milestone merge that adds more than **100 tracked files** requires explicit user review before merge. The preferred result is much smaller.

No previous-project artifact forest is imported into this repository.

## Binary policy

Large upstream/generated model binaries are identified by exact size and cryptographic hashes and normally live in ignored local scratch.

A final deployable TFLite/compiled artifact may be published through a separately approved release/LFS/object-store mechanism. Git must still contain its immutable identity and a deterministic creation procedure.

## Principle

A later reader should be able to answer:

1. What exact upstream object was used?
2. Which script/configuration recreates the accepted artifact?
3. What exact toolchain was used?
4. How should input data be formed?
5. How should output data be interpreted?
6. What numerical/structural facts were accepted?
7. What limitations remain?

without storing every intermediate file used to reach those answers.
