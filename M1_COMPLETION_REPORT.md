# M1 completion report

Disposition: **ACCEPTED**.

The exact official TensorFlow FP32 source was reverified. The archive, extracted SavedModel, embedded StableHLO and labels match their frozen identities. The official full FP32 TFLite was not acquired or used in this rework; its identity remains corroboration only.

The released TensorFlow object mechanically proves the H1 contract. Object-graph list entries `0..805`, concrete-function resource captures and all StableHLO variable argument types align. This proves that StableHLO arguments 450, 451 and 452 bind to the learned GeM exponent, classifier weight and classifier bias only after their computation uses are traced.

The exact H1 path has a shared NHWC `[1,7,9,1280]` feature, learned GeM exponent value `7.408084869384766`, float32 clamp epsilon `1e-6`, NCHW spatial reduction axes `[3,2]`, and an embedding `[1,1280]`. The classifier stores `[11560,1280]`, explicitly transposes to `[1280,11560]`, executes five live deterministic contributions, ordered-sums them and divides by five. The released final sigmoid occurs only after `0.4 / 0.35 / 0.25` three-head fusion. Clean H1 therefore uses the same sigmoid helper on the unweighted H1 logits.

The production slice retains 673 official variables: 670 shared inference tensors and three H1 tensors. StableHLO arguments 453–469 and their H2/H3 computation are absent. Another 116 source variables are inference-dead and intentionally absent. The official label file has exactly 11,560 ordered data rows matching classifier output width.

The adversarial review falsified index-only, shape-only and producer-source assumptions. It also checked axes, layout, hidden cross-head dependencies, evaluation behavior, labels and output semantics. No M1 item remains `UNRESOLVED`.

Authoritative compact evidence:

- `records/m1/SOURCE_IDENTITY.json`
- `records/m1/H1_CONTRACT.json`
- `records/m1/TENSORFLOW_SIGNATURES.json`
- `records/m1/ADVERSARIAL_REVIEW.json`
- `records/m1/ACCEPTED_STATE.json`

M2 entry is authorized. M4 has not started.
