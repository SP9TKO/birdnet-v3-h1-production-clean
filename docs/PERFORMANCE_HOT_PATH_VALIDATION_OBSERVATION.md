# H1 hot-path validation observation

This prospective plan measures waveform validation/setup and the complete post-P0 tail from the same 5 warmup and 20 consecutive diagnostic runs. Its purpose is `HOT_PATH_VALIDATION_DECOMPOSITION_ONLY`. No validation is removed, cached, skipped or relocated.

The authoritative file is `records/performance/HOT_PATH_VALIDATION_OBSERVATION_PLAN_V1.json`. The main criteria remain bound to SHA-256 `b7aa2b1f9ee762b575631be337c66b9f4c4e92bad83f080d7674d20defa6b24b`. The accepted lifecycle implementation is `4cced1f81517bef0e7dcc556f4dedb9d3c8cfb2f`; the accepted bounded heap implementation and entry are `88876bc661cb00c954f8072a369ee37421b8a5f8`.

The outer primary timer starts before the request/run trace and ends after the saved result and ResultReady transition. Full resident waveform CRC and seven checkpoint CRCs are inside this outer interval. Their position outside the narrower `total_compute_cycles` interval does not exclude them from primary latency.

Group W ends immediately before actual frontend invocation. It partitions the resident CRC, expected-identity comparison, retained SHA text, run/profile initialization and outer request setup. These adjoining intervals cover W0 exactly; successful W residual is zero by the prospectively specified timestamp partition. The SHA of uploaded data remains host-declared and echoed; it is not recomputed during inference.

Group R starts at the existing P0 end and ends at the existing outer primary end. It measures profile finalization, each of seven CRC scans, result bookkeeping, prior-result comparison, the saved-result copy, inference-data availability publication and ResultReady trace. Its residual is reconciled independently against the uncovered timestamp gaps. The saved-result copy is itself result publication; there is no duplicate invented publication operation.

Logical validation input is 384,000 waveform bytes and 1,371,312 boundary bytes, totaling 1,755,312 bytes per successful ordinary inference. This is not memory-bus traffic. Diagnostic scan counters must agree with these exact constants and one call at each scan site.

The consumer audit identifies a constraint on a future candidate: ordinary RUN and GET_TOPK expose score CRC, and ordinary repeat_equal depends on all seven checkpoint CRCs. Unguarded GET_RESULT_SUMMARY also exposes all seven. Measurement alone cannot relabel these ordinary result fields as qualification-only. Any combined candidate must independently prove that its deferred operations are not required by ordinary reporting or runtime safety.

Observation adds only diagnostic timestamp/counter storage and an explicit indexed diagnostic export. Existing numerical, lifecycle, top-k, model, cache, copy, frontend and U85 behavior must remain exact. All raw captures and evolving audits remain private. No sub-700 ms acceptance can be claimed from this diagnostic campaign. Candidate optimization implementation is excluded from this session.
