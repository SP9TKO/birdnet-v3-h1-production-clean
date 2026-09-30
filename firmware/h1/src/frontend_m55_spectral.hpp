#pragma once

#include <arm_math.h>

#include <cstddef>
#include <cstdint>

constexpr int H1_M55_FFT_LENGTH = 2048;
constexpr int H1_M55_HOP_LENGTH = 512;
constexpr int H1_M55_FRAME_COUNT = 188;
constexpr int H1_M55_RFFT_BINS = H1_M55_FFT_LENGTH / 2 + 1;

enum H1M55SpectralStage : uint32_t {
	H1_M55_STAGE_NONE = 0,
	H1_M55_STAGE_INIT = 1,
	H1_M55_STAGE_FRAME = 2,
	H1_M55_STAGE_HANN = 3,
	H1_M55_STAGE_CFFT = 4,
	H1_M55_STAGE_REAL_SPLIT = 5,
	H1_M55_STAGE_POWER_HYPOT = 6,
	H1_M55_STAGE_POWER_SQUARES = 7,
	H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED = 8,
};

constexpr uint32_t H1_M55_SPECTRAL_DIAGNOSTIC_MAGIC = UINT32_C(0x53353544);
constexpr uint32_t H1_M55_SPECTRAL_DIAGNOSTIC_VERSION = 1;

struct H1M55SpectralDiagnosticRecord {
	uint32_t magic;
	uint32_t version;
	uint32_t commandSequence;
	uint32_t frameIndex;
	uint32_t stopAfterStage;
	uint32_t lastEnteredStage;
	uint32_t lastCompletedStage;
	uint32_t subIndex;
	uint64_t cycleTimestamp;
	uint32_t exceptionReason;
	uint32_t exceptionNumber;
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t mmfar;
	uint32_t bfar;
	uint32_t stackedPc;
	uint32_t stackedLr;
	uint32_t stackedXpsr;
	uint32_t checksum;
};

struct H1M55SpectralWorkspace {
	alignas(32) float samples[H1_M55_FFT_LENGTH];
};

struct H1M55SpectralContext {
	arm_rfft_fast_instance_f32 rfft;
	arm_cfft_instance_f32 cfft;
	H1M55SpectralWorkspace *workspace;
	bool cfftReady;
};

struct H1M55SpectralTiming {
	uint64_t framePreparationCycles;
	uint64_t hannCycles;
	uint64_t cfftCycles;
	uint64_t realSplitCycles;
	uint64_t transformCycles;
	uint64_t powerHypotCycles;
	uint64_t powerSquaresCycles;
	uint64_t powerCmsisMagSquaredCycles;
	uint64_t totalCycles;
	uint32_t clockHz;
};

struct H1M55SpectralCapture {
	// Optional stage captures. The reconstructed RFFT remains in the workspace.
	float *reflectedFrame;
	float *windowedFrame;
	float *powerHypotSquared;
	float *powerRealSquaredPlusImag;
	H1M55SpectralTiming *timing;
	H1M55SpectralDiagnosticRecord *diagnostic;
};

const char *h1M55SpectralStageName(uint32_t stage);
bool h1M55SpectralDiagnosticValid(const H1M55SpectralDiagnosticRecord *record);
void h1M55SpectralDiagnosticInitialize(H1M55SpectralDiagnosticRecord *record);
void h1M55SpectralDiagnosticBegin(H1M55SpectralDiagnosticRecord *record,
				  uint32_t commandSequence, uint32_t frameIndex,
				  uint32_t stopAfterStage);
void h1M55SpectralDiagnosticMark(H1M55SpectralDiagnosticRecord *record,
				 uint32_t stage, bool completed);
void h1M55SpectralDiagnosticFault(H1M55SpectralDiagnosticRecord *record,
				  uint32_t reason, uint32_t exceptionNumber,
				  uint32_t cfsr, uint32_t hfsr, uint32_t mmfar,
				  uint32_t bfar, uint32_t pc, uint32_t lr,
				  uint32_t xpsr);

bool h1M55SpectralInit(H1M55SpectralContext *context,
		       H1M55SpectralWorkspace *workspace);
bool h1M55SpectralInitFft(H1M55SpectralContext *context);
bool h1M55SpectralPrepare(H1M55SpectralContext *context,
			  H1M55SpectralWorkspace *workspace);
bool h1M55SpectralProcessFrame(H1M55SpectralContext *context,
			       const float *waveform, size_t waveformElements,
			       const float *hann, int frameIndex,
			       const H1M55SpectralCapture *capture,
			       uint32_t stopAfterStage,
			       bool computeScalarPower = true);

// Reconstructed spectrum indices: DC=0, Nyquist=1, and bins k=1..1023
// occupy [2*k, 2*k+1] as real/imaginary. Imaginary DC and Nyquist are zero.
