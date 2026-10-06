// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model_storage.hpp"

#include <cstddef>
#include <cstdint>

enum class H1FrontendStatus : uint8_t {
	Ok,
	InvalidModel,
	InvalidConstants,
	InvalidInput,
	InvalidOutput,
	FftInitializationFailed,
	NonFinite,
};

struct H1M55FrontendRuntime {
	H1M55SpectralContext spectral{};
	const float *hann = nullptr;
	H1M55CompactMel *compactMel = nullptr;
	bool initialized = false;
};

H1FrontendStatus h1InitializeFrontendM55(H1M55FrontendRuntime &runtime,
					 H1M55CompactMel &compactMel);
bool h1FrontendM55Ready(const H1M55FrontendRuntime &runtime);

// Independent pre-promotion TFLM FFT / dense scalar mel reference (Path B).
H1FrontendStatus h1RunFrontendLegacy(const float *waveform, size_t waveformElements,
			       H1FrontendScratch &scratch, float *output,
			       size_t outputElements);

// Ordinary production frontend: native spectral / compact MVE mel (Path C).
H1FrontendStatus h1RunFrontend(const float *waveform, size_t waveformElements,
			     H1FrontendScratch &scratch,
			     H1M55FrontendRuntime &runtime,
			     float *output, size_t outputElements);

// Explicit-diagnostics adapter for the accepted reference spectral path.
H1FrontendStatus h1InitializeReferenceSpectral(H1FrontendScratch &scratch);
H1FrontendStatus h1RunReferenceSpectralFrame(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, int frameIndex);

// Returns the CRC-checked Hann window bound to the frozen V3 frontend model.
bool h1GetFrontendHann(const float **hann);
bool h1GetFrontendMel(const float **mel);
H1FrontendStatus h1RunFrontendM55(const float *waveform, size_t waveformElements,
				   H1FrontendScratch &scratch,
				   H1M55FrontendRuntime &runtime,
				   float *output, size_t outputElements,
				   H1M55FrontendTiming &timing, H1M55FrontendExecution &execution,
				   bool useCmsisPower, bool captureStages);

// Calculates four consecutive compact mel frames in parallel when floating-
// point MVE is available. The scalar fallback retains the same per-frame,
// per-weight accumulation order.
bool h1M55CompactMelFourFrames(const float *powerGroup,
			       const H1M55CompactMel &compactMel,
			       float *output, uint32_t firstFrame,
			       bool &usedMve);
