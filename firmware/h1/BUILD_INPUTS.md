# Local firmware build inputs

CMake requires five local files and checks each file's exact byte count and
SHA-256. These payloads are intentionally absent from Git. A successful build
requires all five; this note does not claim that a fresh public checkout alone
recreates the firmware.

| CMake input | Bytes | SHA-256 | Material and local source |
| --- | ---: | --- | --- |
| `H1_FRONTEND_MODEL_BIN` | 545,776 | `71bd258230cd7897a278132d973e0c9c5ea40a3d0be3ed3a3702175d387732d0` | BirdNET-derived frontend FlatBuffer. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct H1 with `scripts/construct_h1.py`, then derive the frontend FlatBuffer locally. The current public source does not contain a single command that reproduces this exact firmware payload; its expected identity is bound in CMake and the development records. |
| `H1_BACKBONE_MODEL_BIN` | 20,465,840 | `72b80de6c7cf91ae4eb40036d399b4f792ce183d5f629b974f347edec01556c1` | BirdNET-derived Vela-compiled backbone. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct the H1 source, and compile the corresponding backbone with the pinned Vela 5.0.0 configuration. The prior output identity is in CMake and `records/development/VELA_TOOLCHAIN_BINDING.json`; a fresh-checkout builder for this exact payload is not included. |
| `H1_CLASSIFIER_MODEL_BIN` | 13,008,016 | `2ff002cb4bf95b33384d70023a34e377b1d34bb2786e39efc5d358c3f80a6e93` | BirdNET-derived Vela-compiled H1 classifier. Acquire the official Preview 3.1 source with `scripts/bootstrap_m1_m3.sh`, reconstruct the H1 source, and compile the corresponding classifier with the pinned Vela 5.0.0 configuration. The prior output identity is in CMake and `records/development/VELA_TOOLCHAIN_BINDING.json`; a fresh-checkout builder for this exact payload is not included. |
| `H1_SYNTHETIC_BIN` | 384,000 | `23cc7cdce4395c574314856d02fca819871f2809b8b6330ea430b471ed9533f8` | Project-generated canonical float32 waveform. Use `records/m1/fixtures/canonical_waveform.npy`; export its contiguous little-endian float32 payload to an ignored local file. |
| `H1_WREN_BIN` | 384,000 | `46be8bff9ee1fbbb90d66952cc5efb1cde3582ae21862f6744721a1be5183556` | Third-party Eurasian Wren audio fixture, CC BY-SA 3.0. Acquire and prepare locally using `scripts/acquire_biological_audio.py` and `scripts/prepare_biological_windows.py`, following `records/development/BIOLOGICAL_AUDIO_ATTRIBUTION.md`, then export the frozen waveform as contiguous little-endian float32. This is not Apache-licensed project data. |

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

For Wren, first run `scripts/acquire_biological_audio.py` and
`scripts/prepare_biological_windows.py` in the frozen audio environment; the
Wren NPY path is `.development-work/biological-audio/windows/troglodytes_troglodytes_01.npy`.
The public source records exact model payload identities and prior transformation
identities, but does not provide a single fresh-checkout command that creates all
three model binaries. Until that full local route is automated and exercised, a
fresh public firmware reproduction is not claimed.
