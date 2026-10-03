#pragma once

#include <cstddef>
#include <cstdint>

namespace h1diag {

struct FloatBufferErrorMetrics {
	uint32_t elementCount;
	uint32_t referenceFiniteCount;
	uint32_t candidateFiniteCount;
	uint32_t finitePairCount;
	uint32_t referenceCrc32;
	uint32_t candidateCrc32;
	double meanAbsoluteError;
	double rootMeanSquareError;
	double relativeRootMeanSquareError;
	double maximumAbsoluteError;
};

struct Int16BufferErrorMetrics {
	uint32_t elementCount;
	uint32_t mismatchedElements;
	uint32_t maximumAbsoluteDelta;
	uint32_t deltaOneCount;
	uint32_t deltaTwoCount;
	uint32_t deltaGreaterThanTwoCount;
	uint32_t referenceCrc32;
	uint32_t candidateCrc32;
};

bool compareFloatBuffers(const float *reference, const float *candidate,
			 size_t elements, FloatBufferErrorMetrics &metrics);
bool compareInt16Buffers(const int16_t *reference, const int16_t *candidate,
			 size_t elements, Int16BufferErrorMetrics &metrics);

} // namespace h1diag
