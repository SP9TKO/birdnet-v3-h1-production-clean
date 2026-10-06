# Independent scalar oracle V1

The executable arithmetic was independently written from the prospective frozen mathematical contract. Python provides a separate scalar primitive implementation and metadata-only FlatBuffer parsing. The C++ scalar executor executes all 544 backbone and 6 classifier source operators. It uses standard libraries and explicit integer products, shifts, bounds, and saturation. No TensorFlow, TFLite, Ethos-U, Vela, firmware helper, terminal-MUL helper, or historical oracle arithmetic implementation is imported, linked, or executed.

Model constants, serialized quantization metadata, sealed derived coefficients and the normative sigmoid base/slope LUT are shared authority data. The oracle does not decode or execute the U85 command stream. The compiler/static audit is a separate authority-construction stage, excluded from this oracle's executable source manifest.

The frozen primitive corpus supplies all self-test expectations. Both Python and the C++ arithmetic used for full execution are tested. The corpus argument file provided to C++ excludes expected values. A code defect may be fixed while the mathematical contract stays immutable; a true contract defect requires explicit invalidation and a new prospective version.

The dry runner is restricted by design to one canonical regression fixture, with separately frozen deployment and classifier component inputs, matching the qualified Corstone route. It emits exact little-endian INT16 bytes and digests for all 550 operators and four integer boundaries. It does not execute FP32 frontend/GeM composition, a U85 route, or the formal 38-fixture campaign. The prospective GeM bridge remains exact rational binary32 division with nearest ties-to-even and final INT16 clamp; firmware's different halfway behavior is untouched.

Build: `g++ -std=c++17 -O3 -fno-tree-vectorize -fno-fast-math scalar_executor.cpp -o build/scalar_executor`.

Self-tests: `python3 -B test_primitives.py`.

Authorized dry execution: `python3 -B run_oracle.py`.
