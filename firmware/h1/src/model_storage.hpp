#pragma once

#include "audio_contract.hpp"
#include "h1_contract.h"
#include "runtime_profile.h"
#include "frontend_m55_spectral.hpp"

#include <cstddef>
#include <cstdint>

#include <tensorflow/lite/schema/schema_generated.h>

enum class H1NpuStage : uint8_t {
	Backbone,
	Classifier,
};

constexpr size_t H1_M55_MEL_BANDS = 128;
constexpr size_t H1_M55_MEL_MAX_NONZERO_WEIGHTS = 2048;

struct H1M55MelSpan {
	uint16_t startBin;
	uint16_t length;
	uint16_t weightOffset;
};

struct H1M55CompactMel {
	H1M55MelSpan spans[H1_M55_MEL_BANDS];
	float weights[H1_M55_MEL_MAX_NONZERO_WEIGHTS];
	uint32_t weightCount;
	uint32_t ready;
};

struct H1M55FrontendTiming {
	uint64_t frameCycles;
	uint64_t hannCycles;
	uint64_t cfftCycles;
	uint64_t realSplitCycles;
	uint64_t powerCycles;
	uint64_t melCycles;
	uint64_t dbLogCycles;
	uint64_t cropNormalizeCycles;
	uint64_t resizeCycles;
	uint64_t finalLayoutCycles;
	uint64_t totalCycles;
	uint32_t clockHz;
	uint32_t finiteCount;
	uint32_t outputCrc32;
};

enum class H1M55FrontendState : uint8_t {
	Idle,
	Running,
	Success,
	Failed,
	Faulted,
};

enum class H1M55FrontendStage : uint8_t {
	None,
	FrontendEnter,
	SpectralEnter,
	SpectralDone,
	MelEnter,
	MelDone,
	DbEnter,
	DbDone,
	CropNormEnter,
	CropNormDone,
	ResizeEnter,
	ResizeDone,
	LayoutEnter,
	LayoutDone,
	FrontendSuccess,
};

struct H1M55FrontendExecution {
	H1M55FrontendState state;
	H1M55FrontendStage currentStage;
	H1M55FrontendStage failedStage;
	uint32_t reason;
};

struct H1M55SpectralDiagnostics {
	H1M55SpectralContext context;
	float reflected[H1_M55_FFT_LENGTH];
	float windowed[H1_M55_FFT_LENGTH];
	uint32_t powerCanaryBefore;
	float power[H1_M55_RFFT_BINS];
	uint32_t powerCanaryAfter;
	float powerDiagnostic[H1_M55_RFFT_BINS];
	H1M55SpectralTiming timing;
};

struct H1Boundaries {
	alignas(32) float frontend[H1_FRONTEND_ELEMENTS];
	alignas(32) int16_t backboneInput[H1_BACKBONE_INPUT_ELEMENTS];
	alignas(32) int16_t sharedFeature[H1_SHARED_FEATURE_ELEMENTS];
	alignas(32) float embedding[H1_EMBEDDING_ELEMENTS];
	alignas(32) int16_t classifierInput[H1_CLASSIFIER_INPUT_ELEMENTS];
	alignas(32) int16_t logits[H1_LOGIT_ELEMENTS];
	alignas(32) float scores[H1_LOGIT_ELEMENTS];
};

struct H1FrontendScratch {
	alignas(32) float melRaw[188 * 128];
	alignas(32) float melDb[188 * 128];
	alignas(32) float image[125 * 188];
	alignas(32) float gray[224 * 281];
	alignas(32) float frame[2048];
	alignas(32) float spectrum[1025 * 2];
	alignas(32) float power[1025];
	alignas(32) uint8_t fftState[65536];
};

const tflite::Model *h1PrepareNpuModel(H1NpuStage stage,
				       H1ModelLifecycleProfile &profile, bool diagnostics);
bool h1ModelGuardCheck(H1NpuStage stage, unsigned fixture, unsigned run,
		       bool diagnostics);
H1Boundaries &h1CurrentBoundaries();
H1Boundaries &h1FirstBoundaries();
H1FrontendScratch &h1FrontendScratch();
H1M55SpectralDiagnostics &h1M55SpectralDiagnostics();
H1M55SpectralWorkspace &h1M55SpectralWorkspace();
H1M55CompactMel &h1M55CompactMel();
uint8_t *h1UploadPayload();
float *h1UploadedWaveform();
int16_t *h1PcmRingStorage();
int16_t *h1SelectedPcmWindow();
char *h1ProtocolResponse();
