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

struct H1M55CompactScalarMelMetrics {
	uint32_t elementCount;
	uint32_t finiteReference;
	uint32_t finiteCandidate;
	uint32_t finitePairs;
	uint32_t exactBitMatches;
	uint32_t referenceCrc32;
	uint32_t candidateCrc32;
	double meanAbsoluteError;
	double rootMeanSquareError;
	double relativeRootMeanSquareError;
	double maximumAbsoluteError;
};

struct H1M55CompactScalarMelReport {
	uint32_t frameCount;
	uint32_t melBins;
	uint32_t valueCount;
	uint32_t compactWeightCount;
	bool compactStructureValid;
	bool compactWeightIdentityValid;
	uint32_t clockHz;
	uint32_t denseNativePowerCrc32;
	uint32_t compactNativePowerCrc32;
	H1M55CompactScalarMelMetrics isolation;
	H1M55CompactScalarMelMetrics endToEnd;
	uint64_t spectralCycles;
	uint64_t compactMelCycles;
	uint64_t totalCycles;
};

struct H1M55MveCompactMelReport {
	H1M55CompactScalarMelMetrics scalarVsMve;
	H1M55CompactScalarMelMetrics referenceVsMve;
	uint32_t nativePowerCrc32;
	uint32_t referencePowerCrc32;
	uint32_t maximumErrorIndex;
	uint32_t maximumErrorFrame;
	uint32_t maximumErrorBand;
	float maximumErrorScalarValue;
	float maximumErrorMveValue;
	uint32_t clockHz;
	uint32_t usedMve;
	uint64_t scalarCycles;
	uint64_t mveCycles;
	uint32_t frames;
	uint32_t bands;
	uint32_t retainedWeights;
};

bool h1RunCompactScalarMelDiagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	H1M55CompactScalarMelReport &report);
bool h1RunMveCompactMelDiagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	H1M55MveCompactMelReport &report);
