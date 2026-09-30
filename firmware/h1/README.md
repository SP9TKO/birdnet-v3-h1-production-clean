# H1 firmware

This is the self-contained production H1 application source for the Alif U8 Development Kit. It schedules the M55 frontend, U85 backbone, M55 GeM, U85 classifier and M55 score reporting. The `[1,7,9,1280]` shared feature remains an explicit interface.

The default `runOnce()` path uses the reference frontend. The optional `H1_FRONTEND_DIAGNOSTICS=ON` route compares that reference with the production M55 frontend twice, validates output ranges and adjacent guard bytes, and reports complete metrics. Its current disposition is characterization only; promotion is forbidden until a justified tolerance is frozen.

The exact five local payloads and build environment are described in [BUILD_INPUTS.md](BUILD_INPUTS.md). They are not included in Git. The source and paths have been consolidated since the historical development run, so fresh production and diagnostic builds are required. No new physical qualification is claimed.
