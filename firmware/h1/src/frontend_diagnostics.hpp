#pragma once

#include "frontend.hpp"

struct H1M55FrontendComparison {
	H1FrontendStatus referenceStatus;
	H1FrontendStatus productionStatus;
	H1FrontendStatus repeatStatus;
	H1M55FrontendTiming productionTiming;
	H1M55FrontendExecution productionExecution;
	H1M55FrontendExecution repeatExecution;
	uint32_t referenceCrc32;
	uint32_t productionCrc32;
	uint32_t repeatCrc32;
	uint32_t referenceFiniteCount;
	uint32_t productionFiniteCount;
	uint32_t repeatFiniteCount;
	uint32_t exactBitMatches;
	uint32_t mismatchCount;
	uint32_t firstMismatchIndex;
	uint32_t maximumUlpDifference;
	uint32_t repeatExactBitMatches;
	uint32_t repeatMismatchCount;
	uint32_t repeatFirstMismatchIndex;
	double maximumAbsoluteError;
	double meanAbsoluteError;
	double rootMeanSquareError;
	double relativeRootMeanSquareError;
	double cosineSimilarity;
	bool bufferRangesSafe;
	bool guardBytesUnchanged;
	bool metricsAvailable;
};

// Optional development adapter. The spectral and complete frontend algorithms
// remain implemented by the same production core used by the normal build.
H1FrontendStatus h1RunFrontendM55Diagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	float *output, size_t outputElements,
	H1M55FrontendTiming &timing, H1M55FrontendExecution &execution);

bool h1CompareFrontendsM55Diagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	float *referenceOutput, float *productionOutput,
	H1M55FrontendComparison &comparison);
