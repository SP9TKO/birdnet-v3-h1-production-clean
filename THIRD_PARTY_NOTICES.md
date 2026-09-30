# Third-party notices

This repository does not relicense third-party material. The license and
notices supplied by the relevant upstream source, package, SDK, recording, or
model continue to govern that material. The root project licenses apply only
to the original project-authored work described below.

| Material | License or policy |
| --- | --- |
| Original project source code and firmware | Apache-2.0; see [`LICENSE`](LICENSE). This does not extend to linked or embedded third-party material. |
| Original project documentation and original article material | CC BY 4.0; see [`LICENSES/CC-BY-4.0.txt`](LICENSES/CC-BY-4.0.txt). Third-party material embedded in documentation keeps its own terms. |
| BirdNET V3 model and model data | Release-specific Preview 3.1 terms and open issue; see [`MODEL_LICENSES.md`](MODEL_LICENSES.md). |
| BirdNET V3 upstream source code | Upstream MIT license and notices in the official [BirdNET V3 developer-preview repository](https://github.com/birdnet-team/birdnet-V3.0-dev). This does not license its models. |
| Tiny-BirdNET reused mechanisms | Reused mechanisms do not transfer model or data rights. Any source reused from Tiny-BirdNET remains under its Apache-2.0 license and notices. |
| Zephyr | Upstream Zephyr and module licenses/notices; consult the pinned upstream checkout. No complete Zephyr distribution is vendored here. |
| TensorFlow Lite Micro | Upstream component license/notices in the pinned dependency source. |
| CMSIS, CMSIS-DSP, and Ethos-U components | Each upstream component's license/notices in the pinned dependency source. |
| Alif SDK, HAL, and board support | Applicable upstream or vendor license and notices in the pinned SDK checkout. |
| TinyCrypt | Upstream TinyCrypt license/notices in the pinned dependency source. |
| External biological audio | Source-specific terms and attribution; see [`records/development/BIOLOGICAL_AUDIO_ATTRIBUTION.md`](records/development/BIOLOGICAL_AUDIO_ATTRIBUTION.md). Original and transformed audio are not relicensed by this project. |

The local Wren fixture is third-party audio under CC BY-SA 3.0, as recorded in
the audio attribution record. It is not Apache-licensed project data. The
fixture is intentionally absent from Git.

This inventory identifies applicable upstream notice locations; it is not a
legal interpretation, a blanket license for SDK contents, or a grant of
redistribution rights for model or audio material.
