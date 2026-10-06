# Local firmware build inputs

The qualified clean candidate uses [the no-Matter setup](../../records/release/PRODUCTION_SETUP.json) and [flat manifest](workspace/west.yml). These five exact payload gates and the firmware source bundle are unchanged. Historical M5 and the separately qualified candidate retain distinct ELF/BIN/application/ATOC identities.

CMake requires five local files and checks each file's exact byte count and
SHA-256. These payloads are intentionally absent from Git. A successful build
requires all five; this note does not claim that a fresh public checkout alone
recreates the firmware.

| CMake input | Bytes | SHA-256 | Material and local source |
| --- | ---: | --- | --- |
| `H1_FRONTEND_MODEL_BIN` | 545,776 | `71bd258230cd7897a278132d973e0c9c5ea40a3d0be3ed3a3702175d387732d0` | BirdNET-derived frontend FlatBuffer. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct H1 with `scripts/construct_h1.py`, then derive the frontend FlatBuffer locally. The current public source does not contain a single command that reproduces this exact firmware payload; its expected identity is bound in CMake and the development records. |
| `H1_BACKBONE_MODEL_BIN` | 20,465,840 | `f3328db0aa2d7befc13d0f3dd7aa20906ba2164d15f589ddb1f746de398fc2e7` | Accepted V3 BirdNET-derived Vela-compiled backbone. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct the H1 source, and compile the corresponding backbone with the pinned Vela 5.0.0 configuration. The current V3 output identity is bound in CMake; `records/development/VELA_TOOLCHAIN_BINDING.json` retains prior deployment provenance. A fresh-checkout builder for this exact payload is not included. |
| `H1_CLASSIFIER_MODEL_BIN` | 13,008,016 | `2ff002cb4bf95b33384d70023a34e377b1d34bb2786e39efc5d358c3f80a6e93` | BirdNET-derived Vela-compiled H1 classifier. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct the H1 source, and compile the corresponding classifier with the pinned Vela 5.0.0 configuration. The prior output identity is in CMake and `records/development/VELA_TOOLCHAIN_BINDING.json`; a fresh-checkout builder for this exact payload is not included. |
| `H1_SYNTHETIC_BIN` | 384,000 | `23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8` | Project-generated canonical float32 waveform. Use `records/m1/fixtures/canonical_waveform.npy`; export its contiguous little-endian float32 payload to an ignored local file. |
| `H1_WREN_BIN` | 384,000 | `46be8bff9ee1fbbb90d66952cc5efb1cde3582ae21862f6744721a1be5183556` | `LOCALLY_RETAINED_REQUIRED`: exact third-party Eurasian Wren payload under its retained CC BY-SA 3.0 terms. It is required by unchanged fixture embedding and is unavailable from Git alone. No new public acquisition pipeline or raw payload redistribution is authorized. |

The three model payloads contain or derive from BirdNET model data and follow
[`MODEL_LICENSES.md`](../../MODEL_LICENSES.md); the Apache root license does not
cover them. Acquire authoritative BirdNET inputs directly from official
upstream, then reconstruct derived artifacts locally. Do not commit these
payloads or redistribute official or derived model/Vela binaries while the
Preview 3.1 terms discrepancy remains open.

The synthetic waveform's raw identity is retained in
`records/m2/FIXTURE_MANIFEST.json` and `records/development/H1_MODEL_BINDING.json`. The Wren source, frozen interval,
transformation, and attribution are retained in the biological-audio records.
Both binary payloads are local-only build inputs. To export either `.npy` fixture as the raw CMake payload, use the project Python 3.12.14 environment:

```sh
python -c 'import numpy as np; np.ascontiguousarray(np.load("INPUT.npy"), dtype="<f4").tofile("OUTPUT.bin")'
```

For Wren, supply the exact locally retained 384,000-byte payload with SHA-256 `46be8bff9ee1fbbb90d66952cc5efb1cde3582ae21862f6744721a1be5183556` under its original terms. The retained attribution describes historical formation; it does not establish a new public-only formation environment.
The public source records exact model payload identities and prior transformation
identities, but does not provide a single fresh-checkout command that creates all
three model binaries. Until that full local route is automated and exercised, a
fresh public firmware reproduction is not claimed.
