# M2 completion report

Disposition: **ACCEPTED**.

The clean in-memory H1 model is a direct TensorFlow-native slice of the accepted official StableHLO plus exact copies of 673 live official TensorFlow values. It retains the parameterless official waveform frontend, 670 shared inference tensors, learned GeM exponent, H1 classifier weight and H1 classifier bias. H2/H3 arguments 453–469, all H2/H3 computation, 116 inference-dead tensors and the original full-predictor module are absent.

All 673 retained tensors match exactly in shape, dtype, raw SHA-256 and value. Tensor-value identity is reported separately from serialized-file identity; M2 makes no serialized-artifact claim.

Five prospectively frozen valid-domain fixtures were evaluated: canonical non-silent waveform, silence, low-amplitude sine, high-valid-amplitude two-tone and structured impulse. On every fixture:

- official-source probe versus clean copied-value probe was raw-byte equal at shared feature, embedding, H1 logits and H1 scores;
- official-source probe versus the clean production interface was raw-byte equal for embedding and scores;
- clean probe versus clean production was raw-byte equal for embedding and scores;
- NaN and signed-infinity masks matched exactly, and all tested values were finite.

The official public embedding path versus the pruned probe had observed worst maximum absolute error `1.4901161193847656e-08`, worst mean absolute error `1.6079866327345372e-10`, and worst RMSE `1.0574647611818778e-09`.

The independent TensorFlow arithmetic check observed these worst fixture metrics:

| Stage | max absolute error | max mean absolute error | max RMSE |
|---|---:|---:|---:|
| embedding | `5.960464477539063e-08` | `7.247763278428465e-10` | `3.6437141072832567e-09` |
| H1 logits | `3.910064697265625e-05` | `6.998183405523069e-06` | `8.915962392917153e-06` |
| H1 scores | `7.12461769580841e-08` | `5.758345329976531e-10` | `2.313116143740927e-09` |

These are finite observations, not universal bounds. Every value is below its precommitted threshold. The canonical fused public prediction differs from H1-only scores (observed maximum absolute difference `0.011845618486404419`), confirming that fused predictions were not misused as the H1 target.

Static AST inspection and runtime module inspection found no import or execution of `torch`, `torchvision`, `torchaudio`, `onnx` or `onnxruntime`. A second fresh-process validation produced byte-identical evidence files.

No M2 item remains `UNRESOLVED`. `records/m2/ACCEPTED_STATE.json` authorizes M3. M4 has not started.
