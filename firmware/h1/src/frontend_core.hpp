#pragma once

#include "frontend_spectral_core.hpp"
#include <cstddef>
#include <cstdint>

enum class H1FrontendStatus : uint8_t {
	Ok, InvalidModel, InvalidConstants, InvalidInput, InvalidOutput,
	FftInitializationFailed, NonFinite,
};
constexpr size_t H1_M55_MEL_BANDS = 128;
constexpr size_t H1_M55_MEL_MAX_NONZERO_WEIGHTS = 2048;
struct H1M55MelSpan { uint16_t startBin, length, weightOffset; };
struct H1M55CompactMel {
	H1M55MelSpan spans[H1_M55_MEL_BANDS];
	float weights[H1_M55_MEL_MAX_NONZERO_WEIGHTS];
	uint32_t weightCount, ready;
};
struct H1M55FrontendTiming {
	uint64_t frameCycles, hannCycles, cfftCycles, realSplitCycles, powerCycles;
	uint64_t melCycles, dbLogCycles, cropNormalizeCycles, resizeCycles;
	uint64_t finalLayoutCycles, totalCycles;
	uint32_t clockHz, finiteCount, outputCrc32;
};
enum class H1M55FrontendState : uint8_t { Idle, Running, Success, Failed, Faulted };
enum class H1M55FrontendStage : uint8_t {
	None, FrontendEnter, SpectralEnter, SpectralDone, MelEnter, MelDone,
	DbEnter, DbDone, CropNormEnter, CropNormDone, ResizeEnter, ResizeDone,
	LayoutEnter, LayoutDone, FrontendSuccess,
};
struct H1M55FrontendExecution {
	H1M55FrontendState state; H1M55FrontendStage currentStage, failedStage; uint32_t reason;
};
struct H1M55FrontendRuntime {
	H1M55SpectralContext spectral{};
	const float *hann = nullptr;
	H1M55CompactMel *compactMel = nullptr;
	bool initialized = false;
};
struct H1FrontendScratch {
	alignas(32) float melDb[188 * 128];
	alignas(32) float image[125 * 188];
	alignas(32) float gray[224 * 281];
	alignas(32) float frame[2048];
	alignas(32) float spectrum[1025 * 2];
	alignas(32) float power[1025];
	alignas(32) uint8_t fftState[65536];
};
