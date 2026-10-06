# M5 definition

Retained prospective authority SHA-256: `134fc7d1d0e38ab6b24cf17c6c4aa74d08cfab9ba0979fff9493e017b9d2674f`. This document consolidates that definition and the separately frozen accepted M5-4 V2 scope; it adds no numerical tolerance or technical requirement.

| Gate | Exact retained requirement | Authority |
|---|---|---|
| M5-1 | Accepted M4 and exact canonical deployment input binding | ACCEPTED_STATE_INDEX; recovered M4 stop boundary |
| M5-2 | Exact Vela/firmware/runtime/memory/target identity; selected sealed integer arithmetic implementation conformance 0 mismatches / 0 LSB error | D-007; D-020; docs/DEVELOPMENT Phase E |
| M5-3 | Exact source/build/programmed application and ATOC/model-storage identities; explicit descriptors, runner, M55/NPU execution, ownership/completion, shared fan-out | D-017; D-021; exact-artifact rules |
| M5-4 | Successful live uploaded-waveform H1 inference on observed Alif E8 M55-HP/U85-256 target, finite complete vectors, repeats and scoped semantics | D-009 interpreted at accepted uploaded-waveform scope; no live microphone/accuracy claim |
| M5-5 | Compact cumulative qualified evidence and recreation guidance once all hard gates pass | D-002; D-011; D-013; EVIDENCE_RETENTION_POLICY |

M5-4 V1 remains permanently NOT ACCEPTED. The separately prospective V2 criteria bind actual captured integer inputs and exact downstream comparisons, with native frontend and GeM floating limitations preserved. [Qualified evidence](records/m5/QUALIFIED_EVIDENCE.json) records both attempts.

M5-5 requires compact cumulative tracked acceptance/identity/metric records, retained source/configuration, human-readable recreation/input/output guidance, explicit limitations, project/decision/paper reconciliation and retention-policy compliance after all hard gates pass. Its exact retained row has no frozen success enum; this closure adopts `M5_5_CUMULATIVE_EVIDENCE_AND_RECREATION_CLOSURE_PASS`. Commit, push, merge, release tag and binary publication are separate actions requiring explicit authorization.
