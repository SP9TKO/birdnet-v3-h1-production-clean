#pragma once

#include <arm_math.h>
#include <cstddef>
#include <cstdint>

constexpr int H1_M55_FFT_LENGTH = 2048;
constexpr int H1_M55_HOP_LENGTH = 512;
constexpr int H1_M55_FRAME_COUNT = 188;
constexpr int H1_M55_RFFT_BINS = H1_M55_FFT_LENGTH / 2 + 1;

enum H1M55SpectralStage : uint32_t {
	H1_M55_STAGE_NONE = 0, H1_M55_STAGE_INIT = 1, H1_M55_STAGE_FRAME = 2,
	H1_M55_STAGE_HANN = 3, H1_M55_STAGE_CFFT = 4, H1_M55_STAGE_REAL_SPLIT = 5,
	H1_M55_STAGE_POWER_HYPOT = 6, H1_M55_STAGE_POWER_SQUARES = 7,
	H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED = 8,
};

struct H1M55SpectralWorkspace { alignas(32) float samples[H1_M55_FFT_LENGTH]; };
struct H1M55SpectralContext {
	arm_rfft_fast_instance_f32 rfft;
	arm_cfft_instance_f32 cfft;
	H1M55SpectralWorkspace *workspace;
	bool cfftReady;
};
struct H1M55SpectralTiming {
	uint64_t framePreparationCycles, hannCycles, cfftCycles, realSplitCycles;
	uint64_t transformCycles, powerHypotCycles, powerSquaresCycles;
	uint64_t powerCmsisMagSquaredCycles, totalCycles;
	uint32_t clockHz;
};
struct H1M55SpectralCapture {
	float *reflectedFrame;
	float *windowedFrame;
	float *powerHypotSquared;
	float *powerRealSquaredPlusImag;
	H1M55SpectralTiming *timing;
};

bool h1M55SpectralInit(H1M55SpectralContext *, H1M55SpectralWorkspace *);
bool h1M55SpectralInitFft(H1M55SpectralContext *);
bool h1M55SpectralPrepare(H1M55SpectralContext *, H1M55SpectralWorkspace *);
bool h1M55SpectralProcessFrame(H1M55SpectralContext *, const float *, size_t,
	const float *, int, const H1M55SpectralCapture *, uint32_t,
	bool computeScalarPower = true);
