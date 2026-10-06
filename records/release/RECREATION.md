# Qualified no-Matter H1 candidate recreation

Historical M5 remains accepted. Historical Matter provenance remains `MATTER_BINDING_UNRESOLVED_STOP`. The clean candidate is separately `NO_MATTER_PUBLIC_CANDIDATE_QUALIFIED`, with a new firmware identity. [CANDIDATE_AUTHORITY.json](CANDIDATE_AUTHORITY.json) binds exact source, manifest, criteria, candidate artifacts and scope.

## Source and workspace

Use the source tree accompanying this record. Its exact production source material is [the historical source binding](../m5/PRODUCTION_SOURCE_IDENTITY.txt), SHA-256 `485cfc634d27b85d1ebb19c92da85fbe37db277ef7e3b6ac8b403570c20fcd91`. Source-bound `firmware/h1/README.md` and runtime engineering-state strings remain historical snapshots; current status comes from the compact accepted and candidate records.

[PRODUCTION_SETUP.json](PRODUCTION_SETUP.json) supplies the exact Alif E8 DK M55-HP board target, SDK/Zephyr/Alif HAL/Ethos-U revisions, 25 participating repository pins, 24 registered Zephyr modules, all configuration selections, tool identities, command arrays and acquisition classifications. Copy `firmware/h1/workspace/west.yml` to `manifest/west.yml` in a fresh workspace. From that workspace run the declared west init/update recipe. Use the flat manifest as the sole entry point; sdk-alif's upstream imports are not invoked. Matter runtime is NOT USED, its module is ABSENT and it is not a build pin or recreation prerequisite. No bringup branch or private GitHub repository supplies production source.

## Required inputs and tools

The unchanged CMake gates require frontend 545,776 bytes, V3 backbone 20,465,840 bytes, V3 classifier 13,008,016 bytes, synthetic waveform 384,000 bytes and Wren 384,000 bytes, with exact SHA-256 values in PRODUCTION_SETUP.json. Model and deployment payloads retain upstream licensing. Obtain qualified local payloads under their terms, or follow the conditional model reconstruction guidance and retain every immutable hash check. No model or Wren payload is distributed here.

Wren is `LOCALLY_RETAINED_REQUIRED`, SHA-256 `46be8bff9ee1fbbb90d66952cc5efb1cde3582ae21862f6744721a1be5183556`, exactly 384,000 bytes. Its original attribution is in [BIOLOGICAL_AUDIO_ATTRIBUTION.md](../development/BIOLOGICAL_AUDIO_ATTRIBUTION.md). No new public Wren acquisition pipeline or public-only firmware build is claimed. The project-authored canonical `.npy` fixture can be exported as contiguous little-endian float32; its raw hash must equal the declared synthetic input.

The model reconstruction path through M1-M3 remains [REPRODUCIBILITY.md](../../REPRODUCIBILITY.md). Later constructor/emitter/oracle/bridge/harness snapshots are public under `scripts/m5/recreation-layout/`, with exact public paths, restore paths and hashes in [RECREATION_SOURCE_INDEX.json](../m5/RECREATION_SOURCE_INDEX.json). Complete original M4/M5 preflight source/authority maps, accepted GeM payload, sealed arithmetic/primitive corpora, exact licensed runtimes and original orchestration recipes remain explicitly locally retained prerequisites in [RECREATION_GUIDANCE.json](../m5/RECREATION_GUIDANCE.json). They are required for complete original model/qualification replay, rather than a hidden production build source.

Vela is 5.0.0, with qualified Python 3.12.14, exact wheel hashes, dependency lock, official source pin and Dedicated_Sram/U85-256 configuration. Production uses Zephyr SDK 0.17.0, GCC 12.2.0, CMake 4.2.3, Ninja 1.13.2, west 1.5.0 and Ethos-U driver 0.16.0. Full identities and distinct model/build/replay roles are in PRODUCTION_SETUP.json. Historical executable-byte reconstruction is not required. Pinned acquisition classifications are not a new download or executable-rebuild demonstration.

## Build boundary

Use the exact configure argv template and declared environment from PRODUCTION_SETUP.json, substituting your local paths. Pass the semicolon-separated EXTRA_CONF_FILE value as one argv element. Configure is separate from `cmake --build <FRESH_BUILD> --parallel 8`. Keep H1_AUDIO_BACKEND=NONE, H1_FRONTEND_DIAGNOSTICS=ON, the four selected overlays and U85-256. The retained static campaign proved 1,125 surviving Kconfig values identical, DTS byte-identical, 499 compile entries and 70 critical contracts valid, with Matter registration/Kconfig/source/link/init absent.

A freshly generated ELF/BIN has its own identity. The retained qualified candidate ELF/BIN and programmed application/ATOC hashes in CANDIDATE_AUTHORITY.json are evidence, not a promise that a new build emits those bytes. For that candidate's BIN, packaging added 8 zero bytes to reach 1,694,000 bytes; historical M5 BIN packaging used 12. Padding rules are 16-byte alignment with zero bytes. Exact retained application/ATOC programming and any new physical run require separately authorized actions and a new prospective execution identity. Source/build success does not inherit physical acceptance.

## What qualification established

[NO_MATTER_STATIC_BUILD.json](NO_MATTER_STATIC_BUILD.json) and [NO_MATTER_PHYSICAL.json](NO_MATTER_PHYSICAL.json) bind the prospective criteria and final dispositions. Canonical and high_valid_two_tone each ran once plus an immediate repeat: four slots PASS; 12 downstream comparisons / 373,920 integer elements / 0 mismatch / 0 LSB; 8/8 historical boundaries and 8/8 repeat boundaries byte-identical; 8 U85 submissions / 8 completion IRQs; guard/fault/bounds/memory/cache/lifecycle checks PASS; no CPU fallback, unexpected reset or model-storage write.

This did not rerun the historical five-fixture M5 campaign, reaccept historical M5, establish arbitrary-board acceptance or biological accuracy, or establish fresh historical firmware byte equality. Historical M5-4 V1 remains permanently failed and V2 remains separately accepted. GeM FP32 residual is characterization only. [RECREATION_CONTRACT_V1.json](RECREATION_CONTRACT_V1.json) preserves the frozen distinction between REPRODUCED, RECONSTRUCTABLE, PINNED, LOCALLY_RETAINED_REQUIRED and PHYSICAL_ACTION_REQUIRED. Fresh historical firmware-byte reconstruction remains NOT_REQUIRED / UNPROVEN.
