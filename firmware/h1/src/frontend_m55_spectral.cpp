#include "frontend_m55_spectral.hpp"

#include "h1_contract.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>

namespace {

constexpr int kFftLength = H1_M55_FFT_LENGTH;
constexpr int kHalfFftLength = kFftLength / 2;
constexpr int kSpectrumBins = H1_M55_RFFT_BINS;

static_assert(sizeof(H1M55SpectralWorkspace) == 2048u * sizeof(float));
static_assert(alignof(H1M55SpectralWorkspace) >= 32u);
static_assert(sizeof(H1M55SpectralDiagnosticRecord) <= 96u);

int rfftReIndex(int bin)
{
	return bin == 0 ? 0 : (bin == kHalfFftLength ? 1 : 2 * bin);
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

void cfftSplitOneBin(const float *src, const float *twiddle, int halfLength,
		     int bin, float *realOut, float *imagOut)
{
	const int mirror = halfLength - bin;
	const float xAR = src[2 * bin];
	const float xAI = src[2 * bin + 1];
	const float xBR = src[2 * mirror];
	const float xBI = src[2 * mirror + 1];
	const float twR = twiddle[2 * bin];
	const float twI = twiddle[2 * bin + 1];
	const float t1a = xBR - xAR;
	const float t1b = xBI + xAI;
	const float p0 = twR * t1a;
	const float p1 = twI * t1a;
	const float p2 = twR * t1b;
	const float p3 = twI * t1b;
	*realOut = 0.5f * (xAR + xBR + p0 + p3);
	*imagOut = 0.5f * (xAI - xBI + p1 - p2);
}

bool rfftRealSplitForward(const H1M55SpectralContext &context, float *source)
{
	const float *const twiddle = context.rfft.pTwiddleRFFT;
	if (!twiddle) {
		return false;
	}

	const float dcReal = source[0];
	const float dcImag = source[1];
	source[0] = dcReal + dcImag;
	source[1] = dcReal - dcImag;

	for (int bin = 1; bin < kHalfFftLength; ++bin) {
		const int mirror = kHalfFftLength - bin;
		if (bin > mirror) {
			break;
		}
		if (bin == mirror) {
			float real, imag;
			cfftSplitOneBin(source, twiddle, kHalfFftLength, bin, &real, &imag);
			source[2 * bin] = real;
			source[2 * bin + 1] = imag;
			break;
		}

		const float kAR = source[2 * bin];
		const float kAI = source[2 * bin + 1];
		const float mAR = source[2 * mirror];
		const float mAI = source[2 * mirror + 1];
		const float kTwR = twiddle[2 * bin];
		const float kTwI = twiddle[2 * bin + 1];
		const float mTwR = twiddle[2 * mirror];
		const float mTwI = twiddle[2 * mirror + 1];

		const float kT1a = mAR - kAR;
		const float kT1b = mAI + kAI;
		const float kReal = 0.5f * (kAR + mAR + (kTwR * kT1a) + (kTwI * kT1b));
		const float kImag = 0.5f * (kAI - mAI + (kTwI * kT1a) - (kTwR * kT1b));

		const float mT1a = kAR - mAR;
		const float mT1b = kAI + mAI;
		const float mReal = 0.5f * (mAR + kAR + (mTwR * mT1a) + (mTwI * mT1b));
		const float mImag = 0.5f * (mAI - kAI + (mTwI * mT1a) - (mTwR * mT1b));

		source[2 * bin] = kReal;
		source[2 * bin + 1] = kImag;
		source[2 * mirror] = mReal;
		source[2 * mirror + 1] = mImag;
	}

	return true;
}

uint32_t diagnosticChecksum(const H1M55SpectralDiagnosticRecord *record)
{
	const auto *const bytes = reinterpret_cast<const uint8_t *>(record);
	uint32_t hash = UINT32_C(2166136261);
	for (size_t index = 0; index < offsetof(H1M55SpectralDiagnosticRecord, checksum); ++index) {
		hash = (hash ^ bytes[index]) * UINT32_C(16777619);
	}
	return hash;
}

void diagnosticSeal(H1M55SpectralDiagnosticRecord *record)
{
	record->checksum = diagnosticChecksum(record);
}

void recordStage(H1M55SpectralDiagnosticRecord *record, uint32_t stage,
		 bool completed)
{
	if (!record) {
		return;
	}
	if (completed) {
		record->lastCompletedStage = stage;
	} else {
		record->lastEnteredStage = stage;
	}
	record->cycleTimestamp = k_cycle_get_64();
	record->subIndex = UINT32_MAX;
	diagnosticSeal(record);
}

uint64_t completedKernelCycles(const H1M55SpectralTiming &timing)
{
	return timing.framePreparationCycles + timing.hannCycles +
	       timing.cfftCycles + timing.realSplitCycles +
	       timing.powerHypotCycles + timing.powerSquaresCycles +
	       timing.powerCmsisMagSquaredCycles;
}

} // namespace

const char *h1M55SpectralStageName(uint32_t stage)
{
	switch (stage) {
	case H1_M55_STAGE_NONE: return "NONE";
	case H1_M55_STAGE_INIT: return "INIT";
	case H1_M55_STAGE_FRAME: return "FRAME";
	case H1_M55_STAGE_HANN: return "HANN";
	case H1_M55_STAGE_CFFT: return "CFFT";
	case H1_M55_STAGE_REAL_SPLIT: return "REAL_SPLIT";
	case H1_M55_STAGE_POWER_HYPOT: return "POWER_HYPOT";
	case H1_M55_STAGE_POWER_SQUARES: return "POWER_SQUARES";
	case H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED: return "POWER_CMSIS_MAG_SQUARED";
	default: return "UNKNOWN";
	}
}

bool h1M55SpectralDiagnosticValid(const H1M55SpectralDiagnosticRecord *record)
{
	return record && record->magic == H1_M55_SPECTRAL_DIAGNOSTIC_MAGIC &&
	       record->version == H1_M55_SPECTRAL_DIAGNOSTIC_VERSION &&
	       record->checksum == diagnosticChecksum(record);
}

void h1M55SpectralDiagnosticInitialize(H1M55SpectralDiagnosticRecord *record)
{
	if (!record) {
		return;
	}
	std::memset(record, 0, sizeof(*record));
	record->magic = H1_M55_SPECTRAL_DIAGNOSTIC_MAGIC;
	record->version = H1_M55_SPECTRAL_DIAGNOSTIC_VERSION;
	record->subIndex = UINT32_MAX;
	diagnosticSeal(record);
}

void h1M55SpectralDiagnosticBegin(H1M55SpectralDiagnosticRecord *record,
				  uint32_t commandSequence, uint32_t frameIndex,
				  uint32_t stopAfterStage)
{
	h1M55SpectralDiagnosticInitialize(record);
	record->commandSequence = commandSequence;
	record->frameIndex = frameIndex;
	record->stopAfterStage = stopAfterStage;
	record->cycleTimestamp = k_cycle_get_64();
	diagnosticSeal(record);
}

void h1M55SpectralDiagnosticMark(H1M55SpectralDiagnosticRecord *record,
				 uint32_t stage, bool completed)
{
	recordStage(record, stage, completed);
}

void h1M55SpectralDiagnosticFault(H1M55SpectralDiagnosticRecord *record,
				  uint32_t reason, uint32_t exceptionNumber,
				  uint32_t cfsr, uint32_t hfsr, uint32_t mmfar,
				  uint32_t bfar, uint32_t pc, uint32_t lr,
				  uint32_t xpsr)
{
	if (!record) {
		return;
	}
	record->exceptionReason = reason;
	record->exceptionNumber = exceptionNumber;
	record->cfsr = cfsr;
	record->hfsr = hfsr;
	record->mmfar = mmfar;
	record->bfar = bfar;
	record->stackedPc = pc;
	record->stackedLr = lr;
	record->stackedXpsr = xpsr;
	record->cycleTimestamp = k_cycle_get_64();
	diagnosticSeal(record);
}

bool h1M55SpectralInit(H1M55SpectralContext *context,
		       H1M55SpectralWorkspace *workspace)
{
	if (!context || !workspace ||
	    (reinterpret_cast<uintptr_t>(workspace->samples) & 15u) != 0u) {
		return false;
	}
	*context = H1M55SpectralContext{};
	context->workspace = workspace;
	return true;
}

bool h1M55SpectralInitFft(H1M55SpectralContext *context)
{
	if (!context || !context->workspace) {
		return false;
	}

	// Never carry a stale CMSIS instance across a failed or partial init.
	context->cfftReady = false;
	context->rfft = arm_rfft_fast_instance_f32{};
	context->cfft = arm_cfft_instance_f32{};

	if (arm_rfft_fast_init_f32(&context->rfft, kFftLength) != ARM_MATH_SUCCESS) {
		return false;
	}
	if (arm_cfft_init_f32(&context->cfft, kHalfFftLength) != ARM_MATH_SUCCESS) {
		return false;
	}

	context->cfftReady = true;
	return true;
}

bool h1M55SpectralPrepare(H1M55SpectralContext *context,
			  H1M55SpectralWorkspace *workspace)
{
	return h1M55SpectralInit(context, workspace) &&
	       h1M55SpectralInitFft(context);
}

bool h1M55SpectralProcessFrame(H1M55SpectralContext *context,
			       const float *waveform, size_t waveformElements,
			       const float *hann, int frameIndex,
			       const H1M55SpectralCapture *capture,
			       uint32_t stopAfterStage, bool computeScalarPower)
{
	if (!context || !context->workspace || !waveform ||
	    (stopAfterStage >= H1_M55_STAGE_HANN && !hann) ||
	    (stopAfterStage >= H1_M55_STAGE_CFFT && !context->cfftReady) ||
	    waveformElements != H1_WAVEFORM_ELEMENTS || frameIndex < 0 ||
	    frameIndex >= H1_M55_FRAME_COUNT || stopAfterStage < H1_M55_STAGE_FRAME ||
	    stopAfterStage > H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED) {
		return false;
	}

	H1M55SpectralTiming *const timing = capture ? capture->timing : nullptr;
	if (timing) {
		*timing = H1M55SpectralTiming{};
		timing->clockHz = sys_clock_hw_cycles_per_sec();
	}
	float *const workspace = context->workspace->samples;
	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_FRAME, false);
	const uint64_t frameStart = k_cycle_get_64();
	const int paddedStart = (frameIndex + 1) * H1_M55_HOP_LENGTH;
	for (int sample = 0; sample < kFftLength; ++sample) {
		workspace[sample] = reflectedSample(waveform, paddedStart + sample);
	}
	const uint64_t frameEnd = k_cycle_get_64();
	if (capture && capture->reflectedFrame) {
		std::memcpy(capture->reflectedFrame, workspace, sizeof(float) * kFftLength);
	}
	if (timing) {
		timing->framePreparationCycles = frameEnd - frameStart;
		timing->totalCycles = completedKernelCycles(*timing);
	}
	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_FRAME, true);
	if (stopAfterStage == H1_M55_STAGE_FRAME) {
		return true;
	}

	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_HANN, false);
	const uint64_t hannStart = k_cycle_get_64();
	arm_mult_f32(workspace, hann, workspace, kFftLength);
	const uint64_t hannEnd = k_cycle_get_64();
	if (capture && capture->windowedFrame) {
		std::memcpy(capture->windowedFrame, workspace, sizeof(float) * kFftLength);
	}
	if (timing) {
		timing->hannCycles = hannEnd - hannStart;
		timing->totalCycles = completedKernelCycles(*timing);
	}
	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_HANN, true);
	if (stopAfterStage == H1_M55_STAGE_HANN) {
		return true;
	}

	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_CFFT, false);
	const uint64_t cfftStart = k_cycle_get_64();
	arm_cfft_f32(&context->cfft, workspace, 0, 1);
	const uint64_t cfftEnd = k_cycle_get_64();
	if (timing) {
		timing->cfftCycles = cfftEnd - cfftStart;
		timing->totalCycles = completedKernelCycles(*timing);
	}
	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_CFFT, true);
	if (stopAfterStage == H1_M55_STAGE_CFFT) {
		return true;
	}

	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_REAL_SPLIT, false);
	const uint64_t splitStart = k_cycle_get_64();
	if (!rfftRealSplitForward(*context, workspace)) {
		return false;
	}
	const uint64_t splitEnd = k_cycle_get_64();
	if (timing) {
		timing->realSplitCycles = splitEnd - splitStart;
		timing->transformCycles = timing->cfftCycles + timing->realSplitCycles;
		timing->totalCycles = completedKernelCycles(*timing);
	}
	recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_REAL_SPLIT, true);
	if (stopAfterStage == H1_M55_STAGE_REAL_SPLIT) {
		return true;
	}

	if (stopAfterStage == H1_M55_STAGE_POWER_HYPOT) {
		if (!capture || !capture->powerHypotSquared) {
			return false;
		}
		recordStage(capture->diagnostic, H1_M55_STAGE_POWER_HYPOT, false);
		const uint64_t hypotStart = k_cycle_get_64();
		for (int bin = 0; bin < kSpectrumBins; ++bin) {
			const int realIndex = rfftReIndex(bin);
			const float real = workspace[realIndex];
			const float imag = (bin == 0 || bin == kHalfFftLength)
					   ? 0.0f
					   : workspace[realIndex + 1];
			const float magnitude = ::hypotf(real, imag);
			capture->powerHypotSquared[bin] = magnitude * magnitude;
		}
		const uint64_t hypotEnd = k_cycle_get_64();
		if (timing) {
			timing->powerHypotCycles = hypotEnd - hypotStart;
			timing->totalCycles = completedKernelCycles(*timing);
		}
		recordStage(capture->diagnostic, H1_M55_STAGE_POWER_HYPOT, true);
		return true;
	}

	if (computeScalarPower) {
		recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_POWER_SQUARES, false);
		const uint64_t squaresStart = k_cycle_get_64();
		for (int bin = 0; bin < kSpectrumBins; ++bin) {
			const int realIndex = rfftReIndex(bin);
			const float real = workspace[realIndex];
			const float imag = (bin == 0 || bin == kHalfFftLength)
					   ? 0.0f
					   : workspace[realIndex + 1];
			if (capture && capture->powerRealSquaredPlusImag) {
				capture->powerRealSquaredPlusImag[bin] = real * real + imag * imag;
			}
		}
		const uint64_t squaresEnd = k_cycle_get_64();
		if (timing) {
			timing->powerSquaresCycles = squaresEnd - squaresStart;
			timing->totalCycles = completedKernelCycles(*timing);
		}
		recordStage(capture ? capture->diagnostic : nullptr, H1_M55_STAGE_POWER_SQUARES, true);
		if (stopAfterStage == H1_M55_STAGE_POWER_SQUARES) {
			return true;
		}
	}

	if (!capture || !capture->powerRealSquaredPlusImag) {
		return false;
	}
	recordStage(capture->diagnostic, H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, false);
	const uint64_t cmsisStart = k_cycle_get_64();
	capture->powerRealSquaredPlusImag[0] = workspace[0] * workspace[0];
	capture->powerRealSquaredPlusImag[kHalfFftLength] =
		workspace[1] * workspace[1];
	arm_cmplx_mag_squared_f32(workspace + 2,
				 capture->powerRealSquaredPlusImag + 1,
				 kHalfFftLength - 1);
	const uint64_t cmsisEnd = k_cycle_get_64();
	if (timing) {
		timing->powerCmsisMagSquaredCycles = cmsisEnd - cmsisStart;
		timing->totalCycles = completedKernelCycles(*timing);
	}
	recordStage(capture->diagnostic, H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, true);
	return true;
}
