# M5 accepted-state recreation

This document preserves the historical M5 recreation recipe and historical programmed identities/padding. For the separately qualified clean candidate, start with [candidate recreation](../release/RECREATION.md) and [production setup](../release/PRODUCTION_SETUP.json). Matter is absent from that candidate workspace; the historical Matter binding stays unresolved. Exact historical payload programming and fresh candidate construction are separate routes.

The accepted state is hash-bound in [ACCEPTED_IDENTITY_CHAIN.json](ACCEPTED_IDENTITY_CHAIN.json). [RECREATION_GUIDANCE.json](RECREATION_GUIDANCE.json) contains the exact input/output identities, command templates, complete preflight maps and acceptance checks; [TOOLCHAIN_PINS.json](TOOLCHAIN_PINS.json) contains tool versions and binary/wheel hashes.

`REPRODUCED` describes retained demonstrated execution, `RECONSTRUCTABLE` a retained recipe without an additional byte-rebuild claim, and `PINNED` an identity to verify. `LOCALLY_RETAINED_REQUIRED` inputs are intentional prerequisites unavailable from Git alone; `PHYSICAL_ACTION_REQUIRED` steps need separate authorization. No commands below were executed during M5-5.

The Git bootstrap supports M1-M3. M4/M5 source snapshots are copied unchanged under `scripts/m5/recreation-layout/`; restore their indicated relative locations in an isolated recreation checkout. The complete private freeze source/authority maps, accepted GeM payload, sealed arithmetic/primitive corpora, exact licensed tools, original orchestration recipes and exact firmware/package payloads are explicitly required. Do not overwrite accepted evidence or remove preflight checks. A new entry/execution freeze binds a fresh location, Git state and run namespace while preserving every immutable arithmetic/model identity and exact gate.

## Without hardware

### R01 — Retrieve/freeze upstream and canonical M3

`REPRODUCED`, `PINNED`.

- scripts/bootstrap_m1_m3.sh
- Use REPRODUCIBILITY.md sections 6-7 exact constructor/reload commands in a fresh recreation checkout.

M1-M3 fresh reconstruction was established previously. Existing accepted records are verification targets, not new outputs from this closure.

Acceptance: Verify authoritative archive/labels before use; exact wheel locks; all four canonical files and tree hash; H1-only source contract. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R02 — Construct accepted V3 model state

`REPRODUCED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- <TF_PYTHON> <WORK>/m4-shared-feature-closure/construct_v3.py A
- <TF_PYTHON> <WORK>/m4-shared-feature-closure/construct_v3.py B

The accepted constructor reuses the unchanged accepted GeM TFLite as an explicitly required input. This guide does not claim that constructor regenerates GeM from nothing. Restore the complete freeze preflight map, including unused verification-only snapshots; do not remove hash checks.

Acceptance: Require all frozen implementation/authority hashes; canonical calibration aggregate identities; exact V2 control; only permitted tensor-926 scale/max delta; all four V3 bytes equal A/B. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R03 — Reproduce/check quantization-scale derivation

`REPRODUCED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- <TF_PYTHON> <WORK>/m4-shared-feature-closure/derive_v3.py

Derived scales and prospective calibration maximum are fixed authority, not values chosen from physical outputs.

Acceptance: Smallest binary32 scale covers both exact required-domain endpoints; immediate predecessor fails; unchanged quantizer produces accepted scale from the smallest accepted statistic. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R04 — Obtain or reproduce Vela compiled components

`REPRODUCED`, `PINNED`.

- <VELA_PYTHON> -m ethosu.vela <V3_COMPONENT_TFLITE> --accelerator-config ethos-u85-256 --config <DEDICATED_SRAM_INI> --system-config Ethos_U85_SRAM_OSPI --memory-mode Dedicated_Sram --arena-cache-size 524288 --optimise Performance --enable-debug-db --show-cpu-operations --show-subgraph-io-summary --verbose-allocation --verbose-operators --verbose-performance --output-dir <FRESH_COMPONENT_OUTPUT>

A/B compilation of both components already established executable-byte repeatability; path-bearing XML/stdout differed and was not normalized into executable evidence. The published Vela binary wheel was verified, not reproducibly rebuilt from source.

Acceptance: Exact Vela wheel/package/config/runtime identity; A/B executable bytes identical; source and compiled public scales/shape/dtype equal; U85-256; zero CPU fallback. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R05 — Reproduce/check the sealed integer contract

`RECONSTRUCTABLE`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- <VELA_PYTHON> <WORK>/m5-integer-contract/bind_deployment_v1.py
- <VELA_PYTHON> <WORK>/m5-integer-contract/freeze_contract_v1.py

Exact frozen contract/corpus bytes are required. Generators and their complete authority dependencies are retained; independent byte-identical regeneration of timestamped/path-bearing receipts is not established. Never regenerate over the sole accepted freeze or change its identity to get a pass.

Acceptance: All 544 backbone + 6 classifier rules/coefficient domains complete; sealed static-command/source binding; exact contract/corpus hashes; no runtime outputs used as primitive expectations. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R06 — Reproduce independent oracle and primitive checks

`REPRODUCED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- g++ -std=c++17 -O3 -fno-tree-vectorize -fno-fast-math scalar_executor.cpp -o build/scalar_executor
- <HOST_PYTHON> -B <WORK>/m5-integer-contract/oracle-v1/test_primitives.py

An exact historical oracle executable is retained separately. A newly compiled host executable is not promised byte-identical merely because its compiler version matches.

Acceptance: Exact six-source aggregate; standard-library-only arithmetic; no TFLite/Vela/firmware arithmetic linkage; 0 mismatches/0 LSB for both primitive implementations. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R07 — Run host/Corstone M5-2 campaign

`REPRODUCED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- <HOST_PYTHON> -B <WORK>/m5-integer-contract/formal-campaign-v1-route-extended/campaign.py

The historical campaign binds branch, HEAD, Git diff, source identities, directory inodes, old entry and receipt paths. It is an exact retained recipe, not a portable command to run over this completed campaign. Recreate in a separately prepared namespace with prospectively reviewed entry metadata; all model/arithmetic/route hashes and exact checks remain unchanged.

Acceptance: Prospective new entry/execution freeze for the isolated recreation state; unchanged immutable contract, payload, oracle and variable-input route; normal U85 mode; all 38 input pairs executable; stop on first hard failure. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R08 — Build production firmware candidate and ATOC package

`RECONSTRUCTABLE`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- <WEST_PYTHON> -m west build -S ospi-flash --build-dir <FRESH_BUILD> -s <REPO>/firmware/h1 -b alif_e8_dk/ae822fa0e5597xx0/rtss_hp -- -DH1_AUDIO_BACKEND=NONE -DH1_FRONTEND_DIAGNOSTICS=ON -DH1_FRONTEND_MODEL_BIN=<FRONTEND> -DH1_BACKBONE_MODEL_BIN=<V3_BACKBONE> -DH1_CLASSIFIER_MODEL_BIN=<V3_CLASSIFIER> -DH1_SYNTHETIC_BIN=<SYNTHETIC_F32LE> -DH1_WREN_BIN=<WREN_F32LE> -DEXTRA_CONF_FILE=<REPO>/firmware/h1/u85-256.conf;<REPO>/firmware/h1/external-flash.conf;<REPO>/firmware/h1/psram.conf -DETHOSU_TARGET_NPU_CONFIG=ethos-u85-256 -DZEPHYR_SDK_INSTALL_DIR=<ZEPHYR_SDK> -DUSER_CACHE_DIR=<FRESH_CACHE> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
- Pad zephyr.bin to the next 16-byte boundary with zero bytes; retained accepted padding is 12 bytes. From an identity-checked isolated package: ./app-gen-toc -f build/config/app-cfg.json -v

Accepted build PASS does not establish a fresh byte-identical firmware rebuild. Retained ELF/BIN/package are required for exact accepted-state programming; a changed fresh build is a new candidate needing the applicable source/build/programmed/physical checks.

Acceptance: Use west argv array retained below; exact input bindings and original CMake identity material; linked production bridge/runner route; package has H1-HP only at 0x80200000 and ATOC at 0x8057f0d0; compare payloads to retained accepted identities. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

### R09 — Run host production-bridge conformance

`REPRODUCED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- g++ -std=c++17 -O2 -fPIC -shared -fno-fast-math -ffp-contract=off -fsanitize=undefined -fno-sanitize-recover=all -I<REPO>/firmware/h1/src <REPO>/firmware/h1/src/classifier_bridge.cpp <WORK>/m5-firmware-v3/bridge_harness.cpp -o <WORK>/m5-firmware-v3/bridge-host.so
- <HOST_PYTHON> -B <WORK>/m5-firmware-v3/qualify_bridge.py

This is host bridge conformance; physical upstream GeM/integrated execution is covered separately by M5-4 V2.

Acceptance: Independent Fraction reference; exact frozen 38-vector inputs; 0 mismatches/0 LSB and exact saturation counters. Exact inputs, tool references and expected payload hashes are in the matching JSON step.

## Physical hardware

### H01 — Application/ATOC programming and complete readback

`PHYSICAL_ACTION_REQUIRED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- From the exact isolated package directory: ./app-write-mram -p -nr -c <CONFIRMED_SE_PORT>
- Use the retained complete application/ATOC readback command recipes; rehash exact-length results.

No physical action is authorized by this closure document. Separate execution authorization and current physical confirmation are required.

Acceptance: Explicit SW4=SE confirmation; preserve live state before destructive action; app 1,694,000 bytes and ATOC 3,888 bytes exactly equal to frozen sources; programming alone is not boot or inference proof.

### H02 — V3 storage programming/recovery and complete readback

`PHYSICAL_ACTION_REQUIRED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- Apply the exact retained OSPI1_PREPARATION_AUTHORITY sequence under separate hardware authorization; first prove nonwriting loader readiness and complete current storage identity.
- <JFlashExe> -jflashlog<PRIVATE_LOG> -jlinklog<PRIVATE_LOG> -openprj<EXACT_OSPI1_PROJECT> -usb<CONFIRMED_PROBE_SELECTION> -open<V3_BACKBONE_PAYLOAD>,0xC0000000 -auto -exit
- Use retained complete loader readback command recipes for backbone and classifier sector envelopes; compare payload prefix and preserved suffix; then repeat application/ATOC collateral readback.

The accepted classifier bytes were already exact and were not rewritten during the successful backbone recovery. Writing a different board/configuration needs prospective hardware authority.

Acceptance: Exact qualified volatile FIFO-start/flash-reset preparation; never rely on a tool exit code as proof of no write; complete model byte equality; preserve classifier at 0xc2000000 and application/ATOC; no automatic retry or unrelated-bank writes.

### H03 — USB/runtime entry and M5-4 V2 physical execution

`PHYSICAL_ACTION_REQUIRED`, `PINNED`, `LOCALLY_RETAINED_REQUIRED`.

- Retained fresh entry recipe: <HOST_PYTHON> -B <WORK>/m5-physical-v3/v2-execution/entry.py; then the retained ordinary-run/capture/check scripts under a new prospective execution freeze.
- Historical collector command source: <HOST_PYTHON> -B <WORK>/m5-physical-v3/v2-execution/runner.py. Historical safe-resume source: runner_resume_final.py; it requires the preserved first canonical capture and is not a fresh seven-slot entry point.

This accepted campaign retained host checker, GeM-permission and fresh-serial-transcript corrections without rerunning canonical inference. A future fresh campaign must prospectively incorporate those orchestration corrections, create a new unexecuted namespace/freeze, and obtain separate physical authorization. Old V1 captures or historical continuation files cannot count as new V2 runs.

Acceptance: Current identity/readiness/model-storage observations before each slot; exact order canonical, canonical-repeat, silence, low_amplitude_sine, high_valid_two_tone, high_valid_two_tone-repeat, structured_impulse. Capture complete same-run native frontend, deployment input, shared feature, embedding, classifier input, logits and scores; exact native quantization and captured-embedding bridge. Oracles consume actual captured integer inputs; exact backbone/GeM-plus-bridge/classifier boundaries; 8/8 named repeat captures; stage-specific U85 submission/IRQ/queue and integrity proof; stop on first hard failure.

SW4 selection, debugger attach/halt/resume/reset and SoC cold boot are separate operations. When a fresh boot is required, confirm the selector, prepare monitoring, then request reset/power-cycle. The accepted V2 campaign used existing runtime; it does not add a cold-boot claim. MCU Device USB is distinct from the SW4 UART route.

## Scope

- Native Path C frontend is scoped and numerically qualified. Host-reference/native frontend float or deployment-input bit identity is not established.
- M5-4 V1 permanently failed its stronger host-reference deployment-input equality boundary: 2,264 canonical codes differed. V2 prospectively corrected the authority boundary before its separate fresh physical campaign.
- GeM FP32 residual is characterization only. GeM FP32 bit identity and a new floating tolerance are not claimed; classifier-input INT16 exactness is a hard accepted boundary.
- M5-2 qualifies the sealed integer deployment arithmetic on 38 fixtures through the independently qualified Corstone U85 route. M5-4 V2 qualifies actual physical integer inputs and their downstream outputs on five synthetic fixtures and two repeats.
- Physical acceptance is limited to the observed Alif E8 DK M55-HP / Ethos-U85-256, exact firmware, compiled components, configuration and retained runtime. It does not generalize to arbitrary boards or settings.
- Five physical synthetic fixtures do not establish biological accuracy or new V3 biological semantics. Older seven-window B/C evidence stays at its original model/build/runtime/scale.
- No live microphone, acoustic accuracy, whole-domain equivalence, global TensorFlow/deployment equivalence, universal memory safety, final performance/resource proof, H2/H3, regional head, shared-backbone multi-head execution or multi-U85 acceptance is claimed.
- M5-4 V2 used the existing runtime and does not create a new cold-boot proof. Cold boot is optional in the retained contract.
- The waveform SHA-256 was computed/declared by the host and echoed by the board; the board independently checked CRC32. Packet sequence/kind/offset/length/CRC was validated; an original raw packet transcript was not retained.
- Queue registers were observed after the classifier. Backbone completion is bound to stage-specific submission/IRQ hooks and successful queue-wait return, without an intermediate queue-register snapshot claim.
- New firmware byte-identical rebuilds and byte-identical timestamped receipt regeneration have not been established. Retained artifact identities are comparison targets; prospective recreation must keep immutable numerical and payload checks.
