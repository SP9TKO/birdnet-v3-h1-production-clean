# M5 completion report

Date: 2026-10-05. Disposition: **M5 ACCEPTED** at the retained uploaded-waveform H1 scope. M5-5: `M5_5_CUMULATIVE_EVIDENCE_AND_RECREATION_CLOSURE_PASS`.

M5-2: 38/38 fixtures, 152/152 comparisons, 10,727,856 integer elements, 0 mismatches/0 LSB; 8/8 repeat captures byte-identical. M5-3: 271,887 bridge integer comparisons exact, 16 active stale bindings corrected, production build/linked route and complete source→build→programmed chain PASS. M5-4 V2: 5/5 primary fixtures and 7/7 slots, 15 primary/21 total downstream comparisons, 467,400 primary/654,360 total downstream elements exact, 14 U85 submissions/14 completion IRQs, 8/8 repeat captures, scoped memory/integrity PASS.

M5-4 V1 is permanently NOT ACCEPTED (2,264 deployment-input mismatches). V2 corrected the authority boundary prospectively, then used separate new physical captures. Frontend host/native bit identity and GeM FP32 bit identity remain unclaimed; classifier-input INT16 exactness is accepted.

Use [the accepted-state record](records/m5/ACCEPTED_STATE.json), [cumulative gate matrix](records/m5/CUMULATIVE_GATE_MATRIX.json), [identity chain](records/m5/ACCEPTED_IDENTITY_CHAIN.json), [qualified evidence](records/m5/QUALIFIED_EVIDENCE.json) and [recreation guidance](records/m5/RECREATION.md). The single cumulative namespace binds compact derivatives to exact retained receipts. Raw private measurement evidence is preserved; no deletion, new arithmetic, build, oracle campaign, Vela, target access, programming, reset or inference occurred during closure.

The qualified production source remains bundle `485cfc634d27b85d1ebb19c92da85fbe37db277ef7e3b6ac8b403570c20fcd91` on base HEAD `015239f8aef1e32b5fce17bcb539aa004589527a` plus the accepted nine-file delta. Documentation is bound separately; no runtime acceptance-string update or rebuild is needed.

D-001/D-009 make M5 the final defined H1 milestone at accepted live-inference scope. Live microphone/acoustic/biological qualification, performance/resource proof, public release and paper publication remain separate work; these are not silently added M5 hard gates. Tracked evidence is prepared under retention policy, but no commit, push, merge or tag has been performed or authorized.
