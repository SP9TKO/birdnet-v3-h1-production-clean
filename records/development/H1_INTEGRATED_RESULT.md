# Integrated H1 development result

Earlier development work executed the complete prepared-waveform H1 chain on an Alif U8 DK: M55 frontend, U85 backbone, M55 GeM, U85 classifier, and M55 score reporting. It retained the shared `[1,7,9,1280]` feature and reported byte-identical repeated outputs for its development fixtures.

This is historical engineering evidence only. It is not M4/M5 acceptance, a new physical qualification, or a biological-accuracy claim. The firmware was restructured after those runs, so the old build identity is historical and the new public source requires production and diagnostic build revalidation.

The frontend observations retained from that work showed synthetic maximum absolute error `0.000629425048828125`, relative RMS `3.7124729534423126e-6`, cosine `0.9999999999931118`, and frozen Wren maximum absolute error `0.0007762908935546875`, relative RMS `0.0008890160072862032`, cosine `0.9999999946821697` for the earlier native-M55 versus TensorFlow Lite host comparison. Those measurements do not establish a tolerance for the current reconstructed production frontend.
