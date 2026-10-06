// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "frontend.hpp"
#include "baseline_profile.h"
#include "frontend_m55_spectral.hpp"

#include "h1_contract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <flatbuffers/verifier.h>
#include <signal/src/complex.h>
#include <signal/src/rfft.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <zephyr/kernel.h>
#include <arm_math.h>

#if defined(__ARM_FEATURE_MVE) && ((__ARM_FEATURE_MVE & 2) != 0)
#include <arm_mve.h>
#define H1_M55_HAS_MVE_FLOAT 1
#else
#define H1_M55_HAS_MVE_FLOAT 0
#endif

namespace {
constexpr int kFftLength = 2048;
constexpr int kSpectrumBins = 1025;
constexpr int kFrames = 188;
constexpr int kMelBins = 128;
constexpr uint32_t kMelFramesPerMveGroup = 4;
constexpr int kImageRows = 125;
constexpr int kInputWidth = 188;
constexpr int kOutputHeight = 224;
constexpr int kOutputWidth = 281;
constexpr float kLog10Multiplier = 0.4342944622039795f;
constexpr float kDbMultiplier = 10.0f;
constexpr float kDbRange = 100.0f;
constexpr float kEpsilon = 1.000000013351432e-10f;
constexpr float kOne = 1.0f;
constexpr uint32_t kMelCrc32 = UINT32_C(0x88875b16);
constexpr uint32_t kHannCrc32 = UINT32_C(0x8bfb1eaa);

struct FrontendConstants {
	const float *mel;
	const float *hann;
};

// Immutable frontend constants are validated and resolved once per boot.
// These BSS-backed fields are reset by normal startup and therefore must not
// inherit state from the SRAM1 NOLOAD region.
FrontendConstants gFrontendConstants{};
bool gFrontendConstantsReady = false;

uint32_t crc32(const uint8_t *data, size_t bytes)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t index = 0; index < bytes; ++index) {
		crc ^= data[index];
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

uint32_t floatBits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

bool shapeMatches(const tflite::Tensor &tensor, std::initializer_list<int> expected)
{
	if (!tensor.shape() || tensor.shape()->size() != expected.size()) {
		return false;
	}
	unsigned index = 0;
	for (int dimension : expected) {
		if (tensor.shape()->Get(index++) != dimension) {
			return false;
		}
	}
	return true;
}

const uint8_t *constantData(const tflite::Model &model, const tflite::SubGraph &graph,
			    unsigned tensorIndex, tflite::TensorType type,
			    std::initializer_list<int> shape, size_t bytes, uint32_t expectedCrc)
{
	if (!graph.tensors() || tensorIndex >= graph.tensors()->size()) {
		return nullptr;
	}
	const tflite::Tensor *tensor = graph.tensors()->Get(tensorIndex);
	if (!tensor || tensor->type() != type || !shapeMatches(*tensor, shape) ||
	    !model.buffers() || tensor->buffer() >= model.buffers()->size()) {
		return nullptr;
	}
	const tflite::Buffer *buffer = model.buffers()->Get(tensor->buffer());
	if (!buffer || !buffer->data() || buffer->data()->size() != bytes) {
		return nullptr;
	}
	const uint8_t *data = buffer->data()->data();
	return crc32(data, bytes) == expectedCrc ? data : nullptr;
}

bool loadConstants(FrontendConstants &constants)
{
	if (crc32(h1FrontendModelData, H1_FRONTEND_MODEL_BYTES) != H1_FRONTEND_MODEL_CRC32) {
		return false;
	}
	flatbuffers::Verifier verifier(h1FrontendModelData, H1_FRONTEND_MODEL_BYTES);
	if (!tflite::VerifyModelBuffer(verifier)) {
		return false;
	}
	const tflite::Model *model = tflite::GetModel(h1FrontendModelData);
	if (!model || model->version() != TFLITE_SCHEMA_VERSION || !model->subgraphs() ||
	    model->subgraphs()->size() != 1) {
		return false;
	}
	const tflite::SubGraph *graph = model->subgraphs()->Get(0);
	if (!graph || !graph->operators() || graph->operators()->size() != 37 ||
	    !graph->inputs() || graph->inputs()->size() != 1 || graph->inputs()->Get(0) != 0 ||
	    !graph->outputs() || graph->outputs()->size() != 1 ||
	    graph->outputs()->Get(0) != 65) {
		return false;
	}
	const uint8_t *mel = constantData(*model, *graph, 8, tflite::TensorType_FLOAT32,
					  {128, 1025}, 128 * 1025 * sizeof(float), kMelCrc32);
	const uint8_t *hann = constantData(*model, *graph, 28, tflite::TensorType_FLOAT32,
					   {2048}, 2048 * sizeof(float), kHannCrc32);
	if (!mel || !hann || floatBits(kLog10Multiplier) != UINT32_C(0x3ede5bd8) ||
	    floatBits(kDbMultiplier) != UINT32_C(0x41200000) ||
	    floatBits(kDbRange) != UINT32_C(0x42c80000) ||
	    floatBits(kEpsilon) != UINT32_C(0x2edbe6ff)) {
		return false;
	}
	constants.mel = reinterpret_cast<const float *>(mel);
	constants.hann = reinterpret_cast<const float *>(hann);
	return true;
}

bool frontendConstants(const FrontendConstants **constants)
{
	if (!constants) {
		return false;
	}
	if (!gFrontendConstantsReady) {
		FrontendConstants loaded{};
		if (!loadConstants(loaded)) {
			return false;
		}
		gFrontendConstants = loaded;
		gFrontendConstantsReady = true;
	}
	*constants = &gFrontendConstants;
	return true;
}

bool buildCompactMel(const float *mel, H1M55CompactMel &compactMel)
{
	if (!mel) {
		return false;
	}

	// compactMel may reside in NOLOAD SRAM and contain retained bytes after a
	// reset.  Explicitly invalidate it before construction and publish ready
	// only after all descriptors and copied weights have been verified.
	compactMel.ready = 0;
	compactMel.weightCount = 0;

	uint32_t weightCount = 0;
	for (int band = 0; band < kMelBins; ++band) {
		const float *row = mel + band * kSpectrumBins;
		int first = 0;
		while (first < kSpectrumBins && row[first] == 0.0f) {
			++first;
		}
		int end = kSpectrumBins;
		while (end > first && row[end - 1] == 0.0f) {
			--end;
		}

		if (first == end) {
			return false;
		}
		const uint32_t length = static_cast<uint32_t>(end - first);
		if (weightCount + length > H1_M55_MEL_MAX_NONZERO_WEIGHTS ||
		    first > UINT16_MAX || length > UINT16_MAX || weightCount > UINT16_MAX) {
			return false;
		}

		const uint32_t offset = weightCount;
		for (int bin = first; bin < end; ++bin) {
			const float weight = row[bin];
			// The compact representation assumes exactly one contiguous non-zero
			// interval for every mel band.
			if (weight == 0.0f || !std::isfinite(weight)) {
				return false;
			}
			compactMel.weights[weightCount++] = weight;
		}

		compactMel.spans[band] = H1M55MelSpan{
			static_cast<uint16_t>(first),
			static_cast<uint16_t>(length),
			static_cast<uint16_t>(offset)};
	}

	// Re-validate the complete table against the authoritative frozen matrix
	// before making it visible to the runtime path.
	uint32_t expectedOffset = 0;
	for (int band = 0; band < kMelBins; ++band) {
		const H1M55MelSpan &span = compactMel.spans[band];
		if (span.length == 0 || span.startBin >= kSpectrumBins ||
		    static_cast<uint32_t>(span.startBin) + span.length > kSpectrumBins ||
		    span.weightOffset != expectedOffset ||
		    static_cast<uint32_t>(span.weightOffset) + span.length > weightCount) {
			return false;
		}

		const float *source = mel + band * kSpectrumBins + span.startBin;
		const float *weights = compactMel.weights + span.weightOffset;
		for (uint32_t i = 0; i < span.length; ++i) {
			if (floatBits(source[i]) != floatBits(weights[i])) {
				return false;
			}
		}
		expectedOffset += span.length;
	}
	if (expectedOffset != weightCount) {
		return false;
	}

	compactMel.weightCount = weightCount;
	compactMel.ready = 1;
	return true;
}

float reflectedSample(const float *waveform, int paddedIndex)
{
	if (paddedIndex < 1536) {
		return waveform[1536 - paddedIndex];
	}
	if (paddedIndex < 97536) {
		return waveform[paddedIndex - 1536];
	}
	return waveform[95998 - (paddedIndex - 97536)];
}

void resizeHalfPixel(const float *input, float *output)
{
	const float heightScale = static_cast<float>(kImageRows) / kOutputHeight;
	const float widthScale = static_cast<float>(kInputWidth) / kOutputWidth;
	for (int y = 0; y < kOutputHeight; ++y) {
		const float inputY = (static_cast<float>(y) + 0.5f) * heightScale - 0.5f;
		const float inputYFloor = std::floor(inputY);
		const int y0 = std::max(static_cast<int>(inputYFloor), 0);
		const int y1 = std::min(static_cast<int>(std::ceil(inputY)), kImageRows - 1);
		for (int x = 0; x < kOutputWidth; ++x) {
			const float inputX = (static_cast<float>(x) + 0.5f) * widthScale - 0.5f;
			const float inputXFloor = std::floor(inputX);
			const int x0 = std::max(static_cast<int>(inputXFloor), 0);
			const int x1 = std::min(static_cast<int>(std::ceil(inputX)), kInputWidth - 1);
			const float dy = inputY - static_cast<float>(y0);
			const float dx = inputX - static_cast<float>(x0);
			output[y * kOutputWidth + x] =
				input[y0 * kInputWidth + x0] * (1.0f - dy) * (1.0f - dx) +
				input[y1 * kInputWidth + x0] * dy * (1.0f - dx) +
				input[y0 * kInputWidth + x1] * (1.0f - dy) * dx +
				input[y1 * kInputWidth + x1] * dy * dx;
		}
	}
}
} // namespace

H1FrontendStatus h1RunFrontendLegacy(const float *waveform, size_t waveformElements,
			       H1FrontendScratch &scratch, float *output,
			       size_t outputElements)
{
	static_assert(sizeof(float) == 4);
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS) {
		return H1FrontendStatus::InvalidInput;
	}
	if (!output || outputElements != H1_FRONTEND_ELEMENTS) {
		return H1FrontendStatus::InvalidOutput;
	}
	const FrontendConstants *constants = nullptr;
	if (!frontendConstants(&constants)) {
		return H1FrontendStatus::InvalidConstants;
	}
	const size_t needed = tflm_signal::RfftFloatGetNeededMemory(kFftLength);
	if (needed > sizeof(scratch.fftState) ||
	    !tflm_signal::RfftFloatInit(kFftLength, scratch.fftState,
					 needed)) {
		return H1FrontendStatus::FftInitializationFailed;
	}
	auto *spectrum = reinterpret_cast<Complex<float> *>(scratch.spectrum);
	float globalMaximum = -INFINITY;
	for (int frameIndex = 0; frameIndex < kFrames; ++frameIndex) {
		const int paddedStart = (frameIndex + 1) * 512;
		for (int sample = 0; sample < kFftLength; ++sample) {
			scratch.frame[sample] =
				reflectedSample(waveform, paddedStart + sample) * constants->hann[sample];
		}
		tflm_signal::RfftFloatApply(scratch.fftState, scratch.frame, spectrum);
		for (int bin = 0; bin < kSpectrumBins; ++bin) {
			const float magnitude = ::hypotf(spectrum[bin].real, spectrum[bin].imag);
			scratch.power[bin] = magnitude * magnitude;
		}
		for (int mel = 0; mel < kMelBins; ++mel) {
			float sum = 0.0f;
			const float *weights = constants->mel + mel * kSpectrumBins;
			for (int bin = 0; bin < kSpectrumBins; ++bin) {
				sum += scratch.power[bin] * weights[bin];
			}
			const float positive = sum > kEpsilon ? sum : kEpsilon;
			const float db = ::logf(positive) * kLog10Multiplier * kDbMultiplier;
			scratch.melDb[frameIndex * kMelBins + mel] = db;
			globalMaximum = db > globalMaximum ? db : globalMaximum;
		}
	}
	const float floorValue = globalMaximum - kDbRange;
	float imageMinimum = INFINITY;
	float imageMaximum = -INFINITY;
	for (int row = 0; row < kImageRows; ++row) {
		const int mel = 125 - row;
		for (int time = 0; time < kInputWidth; ++time) {
			const float db = scratch.melDb[time * kMelBins + mel];
			const float clamped = db > floorValue ? db : floorValue;
			scratch.image[row * kInputWidth + time] = clamped;
			imageMinimum = clamped < imageMinimum ? clamped : imageMinimum;
			imageMaximum = clamped > imageMaximum ? clamped : imageMaximum;
		}
	}
	const float denominator = (imageMaximum - imageMinimum) + kEpsilon;
	for (float &value : scratch.image) {
		value = kOne - ((value - imageMinimum) / denominator);
	}
	resizeHalfPixel(scratch.image, scratch.gray);
	constexpr float means[3] = {0.5f, 0.4000000059604645f, 0.30000001192092896f};
	constexpr float inverseStd[3] = {2.0f, 3.3333332538604736f, 10.0f};
	for (int pixel = 0; pixel < kOutputHeight * kOutputWidth; ++pixel) {
		for (int channel = 0; channel < 3; ++channel) {
			const float value = (scratch.gray[pixel] - means[channel]) * inverseStd[channel];
			if (!std::isfinite(value)) {
				return H1FrontendStatus::NonFinite;
			}
			output[pixel * 3 + channel] = value;
		}
	}
	return H1FrontendStatus::Ok;
}

H1FrontendStatus h1InitializeReferenceSpectral(H1FrontendScratch &scratch)
{
	const size_t needed = tflm_signal::RfftFloatGetNeededMemory(kFftLength);
	if (needed > sizeof(scratch.fftState) ||
	    !tflm_signal::RfftFloatInit(kFftLength, scratch.fftState, needed)) {
		return H1FrontendStatus::FftInitializationFailed;
	}
	return H1FrontendStatus::Ok;
}

H1FrontendStatus h1RunReferenceSpectralFrame(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, int frameIndex)
{
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS ||
	    frameIndex < 0 || frameIndex >= kFrames) {
		return H1FrontendStatus::InvalidInput;
	}
	const FrontendConstants *constants = nullptr;
	if (!frontendConstants(&constants)) {
		return H1FrontendStatus::InvalidConstants;
	}
	auto *const spectrum = reinterpret_cast<Complex<float> *>(scratch.spectrum);
	const int paddedStart = (frameIndex + 1) * 512;
	for (int sample = 0; sample < kFftLength; ++sample) {
		scratch.frame[sample] =
			reflectedSample(waveform, paddedStart + sample) * constants->hann[sample];
	}
	tflm_signal::RfftFloatApply(scratch.fftState, scratch.frame, spectrum);
	for (int bin = 0; bin < kSpectrumBins; ++bin) {
		const float magnitude = ::hypotf(spectrum[bin].real, spectrum[bin].imag);
		scratch.power[bin] = magnitude * magnitude;
	}
	return H1FrontendStatus::Ok;
}

bool h1GetFrontendMel(const float **mel)
{
	if (!mel) return false;
	const FrontendConstants *constants = nullptr;
	if (!frontendConstants(&constants)) return false;
	*mel = constants->mel;
	return true;
}

H1FrontendStatus h1InitializeFrontendM55(H1M55FrontendRuntime &runtime,
					 H1M55CompactMel &compactMel)
{
	runtime = H1M55FrontendRuntime{};

	const FrontendConstants *constants = nullptr;
	if (!frontendConstants(&constants)) {
		return H1FrontendStatus::InvalidConstants;
	}

	// compactMel may be linker-owned NOLOAD SRAM. Rebuild it deterministically
	// once per boot; never treat retained bytes as initialization state.
	if (!buildCompactMel(constants->mel, compactMel)) {
		return H1FrontendStatus::InvalidConstants;
	}

	if (!h1M55SpectralPrepare(&runtime.spectral, &h1M55SpectralWorkspace())) {
		return H1FrontendStatus::FftInitializationFailed;
	}

	runtime.hann = constants->hann;
	runtime.compactMel = &compactMel;
	runtime.initialized = true;
	return H1FrontendStatus::Ok;
}

bool h1FrontendM55Ready(const H1M55FrontendRuntime &runtime)
{
	return runtime.initialized && runtime.hann && runtime.compactMel &&
	       runtime.compactMel->ready == 1 && runtime.compactMel->weightCount > 0 &&
	       runtime.compactMel->weightCount <= H1_M55_MEL_MAX_NONZERO_WEIGHTS &&
	       runtime.spectral.workspace == &h1M55SpectralWorkspace() &&
	       runtime.spectral.cfftReady;
}

bool h1M55CompactMelFourFrames(const float *powerGroup,
			       const H1M55CompactMel &compactMel,
			       float *output, uint32_t firstFrame,
			       bool &usedMve)
{
	if (!powerGroup || !output || compactMel.ready != 1 ||
	    compactMel.weightCount == 0 ||
	    compactMel.weightCount > H1_M55_MEL_MAX_NONZERO_WEIGHTS ||
	    firstFrame + kMelFramesPerMveGroup > kFrames) {
		return false;
	}

#if H1_M55_HAS_MVE_FLOAT
	static constexpr uint32_t frameOffsets[kMelFramesPerMveGroup] = {
		0u, kSpectrumBins, 2u * kSpectrumBins, 3u * kSpectrumBins};
	const uint32x4_t laneOffsets = vld1q_u32(frameOffsets);
	for (int band = 0; band < kMelBins; ++band) {
		const H1M55MelSpan &span = compactMel.spans[band];
		const uint32x4_t offsets = vaddq_n_u32(laneOffsets, span.startBin);
		float32x4_t sum = vdupq_n_f32(0.0f);
		for (uint32_t i = 0; i < span.length; ++i) {
			const float32x4_t power =
				vldrwq_gather_shifted_offset_f32(powerGroup + i, offsets);
			const float32x4_t weight = vdupq_n_f32(
				compactMel.weights[span.weightOffset + i]);
			const float32x4_t product = vmulq_f32(power, weight);
			sum = vaddq_f32(sum, product);
		}
		output[(firstFrame + 0u) * kMelBins + band] = vgetq_lane_f32(sum, 0);
		output[(firstFrame + 1u) * kMelBins + band] = vgetq_lane_f32(sum, 1);
		output[(firstFrame + 2u) * kMelBins + band] = vgetq_lane_f32(sum, 2);
		output[(firstFrame + 3u) * kMelBins + band] = vgetq_lane_f32(sum, 3);
	}
	usedMve = true;
#else
	for (uint32_t lane = 0; lane < kMelFramesPerMveGroup; ++lane) {
		const float *const framePower = powerGroup + lane * kSpectrumBins;
		for (int band = 0; band < kMelBins; ++band) {
			const H1M55MelSpan &span = compactMel.spans[band];
			float sum = 0.0f;
			for (uint32_t i = 0; i < span.length; ++i) {
				sum += framePower[span.startBin + i] *
				       compactMel.weights[span.weightOffset + i];
			}
			output[(firstFrame + lane) * kMelBins + band] = sum;
		}
	}
	usedMve = false;
#endif
	return true;
}

H1FrontendStatus h1RunFrontendM55(const float *waveform, size_t waveformElements,
				   H1FrontendScratch &scratch,
				   H1M55FrontendRuntime &runtime,
				   float *output, size_t outputElements,
				   H1M55FrontendTiming &timing, H1M55FrontendExecution &execution,
				   bool useCmsisPower, bool captureStages)
{
	auto mark = [&execution](H1M55FrontendStage stage) {
		execution.currentStage = stage;
	};
	auto fail = [&execution](H1FrontendStatus status) {
		execution.failedStage = execution.currentStage;
		execution.reason = static_cast<uint32_t>(status);
		execution.state = H1M55FrontendState::Failed;
		return status;
	};
	execution = H1M55FrontendExecution{
		H1M55FrontendState::Running, H1M55FrontendStage::FrontendEnter,
		H1M55FrontendStage::None, 0};
	mark(H1M55FrontendStage::FrontendEnter);
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS)
		return fail(H1FrontendStatus::InvalidInput);
	if (!output || outputElements != H1_FRONTEND_ELEMENTS)
		return fail(H1FrontendStatus::InvalidOutput);
	if (!h1FrontendM55Ready(runtime))
		return fail(H1FrontendStatus::FftInitializationFailed);

	const float *const hann = runtime.hann;
	H1M55CompactMel &compactMel = *runtime.compactMel;

	timing = H1M55FrontendTiming{};
	timing.clockHz = sys_clock_hw_cycles_per_sec();
	const uint64_t totalStart = k_cycle_get_64();

	static_assert(kFrames % kMelFramesPerMveGroup == 0);
	static_assert(kMelFramesPerMveGroup * kSpectrumBins <= 224 * 281);
	float *const groupPower = scratch.gray;
	for (int firstFrame = 0; firstFrame < kFrames;
	     firstFrame += kMelFramesPerMveGroup) {
		mark(H1M55FrontendStage::SpectralEnter);
		h1BaselineSpectralGroupBegin();
		for (uint32_t lane = 0; lane < kMelFramesPerMveGroup; ++lane) {
			const uint32_t frame = firstFrame + lane;
			H1M55SpectralTiming frameTiming{};
			float *const framePower = groupPower + lane * kSpectrumBins;
			H1M55SpectralCapture capture{nullptr, nullptr, nullptr, framePower,
					     &frameTiming, nullptr};
			const uint32_t powerStage = useCmsisPower
				? H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED
				: H1_M55_STAGE_POWER_SQUARES;
			if (!h1M55SpectralProcessFrame(&runtime.spectral, waveform,
						       waveformElements, hann, frame, &capture,
						       powerStage, !useCmsisPower))
				return fail(H1FrontendStatus::InvalidInput);
			timing.frameCycles += frameTiming.framePreparationCycles;
			timing.hannCycles += frameTiming.hannCycles;
			timing.cfftCycles += frameTiming.cfftCycles;
			timing.realSplitCycles += frameTiming.realSplitCycles;
			timing.powerCycles += useCmsisPower
				? frameTiming.powerCmsisMagSquaredCycles
				: frameTiming.powerSquaresCycles;
		}
		h1BaselineSpectralGroupEnd();
		mark(H1M55FrontendStage::SpectralDone);
		mark(H1M55FrontendStage::MelEnter);
		const uint64_t melStart = k_cycle_get_64();
		bool usedMve = false;
		if (!h1M55CompactMelFourFrames(groupPower, compactMel,
					       scratch.melDb, firstFrame, usedMve))
			return fail(H1FrontendStatus::InvalidConstants);
		(void)usedMve;
		for (uint32_t lane = 0; lane < kMelFramesPerMveGroup; ++lane) {
			const size_t offset =
				(firstFrame + lane) * kMelBins;
			for (int band = 0; band < kMelBins; ++band) {
				if (!std::isfinite(scratch.melDb[offset + band]))
					return fail(H1FrontendStatus::NonFinite);
			}
			if (captureStages) {
				std::memcpy(scratch.melRaw + offset,
					    scratch.melDb + offset,
					    kMelBins * sizeof(float));
			}
		}
		timing.melCycles += k_cycle_get_64() - melStart;
		mark(H1M55FrontendStage::MelDone);
	}
	mark(H1M55FrontendStage::SpectralDone);

	h1BaselineMelCycles(timing.melCycles);
	mark(H1M55FrontendStage::DbEnter);
	float globalMaximum = -INFINITY;
	const uint64_t dbStart = k_cycle_get_64();
	for (int i = 0; i < kFrames * kMelBins; ++i) {
		const float sum = scratch.melDb[i];
		const float positive = sum > kEpsilon ? sum : kEpsilon;
		const float db = ::logf(positive) * kLog10Multiplier * kDbMultiplier;
		scratch.melDb[i] = db;
		globalMaximum = db > globalMaximum ? db : globalMaximum;
	}
	const float floorValue = globalMaximum - kDbRange;
	timing.dbLogCycles = k_cycle_get_64() - dbStart;
	mark(H1M55FrontendStage::DbDone);

	mark(H1M55FrontendStage::CropNormEnter);
	const uint64_t cropStart = k_cycle_get_64();
	float imageMinimum = INFINITY;
	float imageMaximum = -INFINITY;
	for (int row = 0; row < kImageRows; ++row) {
		const int band = 125 - row;
		for (int time = 0; time < kInputWidth; ++time) {
			const float db = scratch.melDb[time * kMelBins + band];
			const float clamped = db > floorValue ? db : floorValue;
			scratch.image[row * kInputWidth + time] = clamped;
			imageMinimum = clamped < imageMinimum ? clamped : imageMinimum;
			imageMaximum = clamped > imageMaximum ? clamped : imageMaximum;
		}
	}
	const float denominator = (imageMaximum - imageMinimum) + kEpsilon;
	for (float &value : scratch.image)
		value = kOne - ((value - imageMinimum) / denominator);
	timing.cropNormalizeCycles = k_cycle_get_64() - cropStart;
	mark(H1M55FrontendStage::CropNormDone);

	mark(H1M55FrontendStage::ResizeEnter);
	const uint64_t resizeStart = k_cycle_get_64();
	resizeHalfPixel(scratch.image, scratch.gray);
	timing.resizeCycles = k_cycle_get_64() - resizeStart;
	mark(H1M55FrontendStage::ResizeDone);

	mark(H1M55FrontendStage::LayoutEnter);
	const uint64_t layoutStart = k_cycle_get_64();
	constexpr float means[3] = {0.5f, 0.4000000059604645f, 0.30000001192092896f};
	constexpr float inverseStd[3] = {2.0f, 3.3333332538604736f, 10.0f};
	for (int pixel = 0; pixel < kOutputHeight * kOutputWidth; ++pixel) {
		for (int channel = 0; channel < 3; ++channel) {
			output[pixel * 3 + channel] =
				(scratch.gray[pixel] - means[channel]) * inverseStd[channel];
		}
	}
	timing.finalLayoutCycles = k_cycle_get_64() - layoutStart;
	mark(H1M55FrontendStage::LayoutDone);
	timing.totalCycles = k_cycle_get_64() - totalStart;

	timing.finiteCount = 0;
	for (size_t i = 0; i < outputElements; ++i)
		timing.finiteCount += std::isfinite(output[i]);
	timing.outputCrc32 = crc32(reinterpret_cast<const uint8_t *>(output),
				   outputElements * sizeof(float));
	if (timing.finiteCount != outputElements)
		return fail(H1FrontendStatus::NonFinite);
	mark(H1M55FrontendStage::FrontendSuccess);
	execution.state = H1M55FrontendState::Success;
	execution.reason = 0;
	return H1FrontendStatus::Ok;
}

bool h1GetFrontendHann(const float **hann)
{
	if (!hann) {
		return false;
	}
	const FrontendConstants *constants = nullptr;
	if (!frontendConstants(&constants)) {
		return false;
	}
	*hann = constants->hann;
	return true;
}

H1FrontendStatus h1RunFrontend(const float *waveform, size_t waveformElements,
			     H1FrontendScratch &scratch,
			     H1M55FrontendRuntime &runtime,
			     float *output, size_t outputElements)
{
	H1M55FrontendTiming timing{};
	H1M55FrontendExecution execution{};
	// Production uses the promoted native spectral / compact MVE mel route.
	return h1RunFrontendM55(waveform, waveformElements, scratch, runtime,
		output, outputElements, timing, execution, false, false);
}
