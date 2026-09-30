# M2 — clean TensorFlow FP32 H1 reproduction

State: **ACCEPTED**. M1 entry was accepted, and numerical execution was adjudicated only against the prospectively committed `records/m2/PROSPECTIVE_CRITERIA.json`.

## Claim

M2 constructs an in-memory clean TensorFlow FP32 H1 model directly from the accepted official TensorFlow FP32 SavedModel. It retains the exact official waveform frontend, slices the embedded StableHLO by dependency, copies exact values for only the 673 live shared/H1 tensors, and contains no live or stored H2/H3 learned state.

No PyTorch, ONNX, FP16-pruned model, retraining, class removal or algebraic collapse participates. In particular, the five official evaluation classifier contributions remain five ordered operations even though they share values.

## Construction

`scripts/construct_h1.py` must:

1. verify the exact accepted `saved_model.pb` and StableHLO identities;
2. trace H1 from the official return graph rather than names or shapes;
3. route the official sigmoid helper to unweighted H1 logits;
4. backward-slice the production outputs (`scores`, `embedding`);
5. remove arguments 453–469 and every other unreachable variable/operation/function;
6. retain only official TensorFlow variable values selected by the slice;
7. build a fresh `tf.Module` with no parent full-model object.

The validation-only probe exports shared feature, embedding, H1 logits and H1 scores. The production module exports only scores `[1,11560]` and embedding `[1,1280]`.

## Prospective validation

The five fixtures and every numerical threshold were frozen before final comparisons. Exact raw-byte equality is required for every official-probe versus clean-probe stage because the exact module and copied values are reused. Separate strict FP32 tolerances cover production-slice/DCE execution, the official public embedding path and independent TensorFlow arithmetic checks.

Every comparison records maximum absolute error, mean absolute error, RMSE, a symmetric relative metric, exact-equality count, raw-byte equality and nonfinite masks. Measured maxima are observations over the frozen fixtures, never universal bounds.

## Structural acceptance

Acceptance additionally requires:

- 673/673 retained tensors have exact shapes, dtypes, raw hashes and values;
- no H2/H3-specific argument or computation is live;
- the full-predictor StableHLO is absent from the clean graph;
- only the pruned H1 StableHLO payload is callable;
- output and label widths are exact;
- the exact official CSV defines positional class order;
- static AST and runtime checks find no PyTorch/ONNX dependency;
- adversarial review has no `UNRESOLVED` item.

M3 is unauthorized until `records/m2/ACCEPTED_STATE.json` records an accepted disposition.
