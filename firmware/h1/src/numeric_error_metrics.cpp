#include "numeric_error_metrics.hpp"

#include <cmath>
#include <limits>

namespace h1diag {
namespace {

uint32_t crc32(const void *data, size_t bytes)
{
	const auto *raw = static_cast<const uint8_t *>(data);
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t index = 0; index < bytes; ++index) {
		crc ^= raw[index];
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^
			      (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

} // namespace

bool compareFloatBuffers(const float *reference, const float *candidate,
			 size_t elements, FloatBufferErrorMetrics &metrics)
{
	metrics = FloatBufferErrorMetrics{};
	if (!reference || !candidate || elements == 0 ||
	    elements > UINT32_MAX || elements > SIZE_MAX / sizeof(float)) {
		return false;
	}

	metrics.elementCount = static_cast<uint32_t>(elements);
	metrics.referenceCrc32 = crc32(reference, elements * sizeof(float));
	metrics.candidateCrc32 = crc32(candidate, elements * sizeof(float));
	double absoluteSum = 0.0;
	double errorSquares = 0.0;
	double referenceSquares = 0.0;

	for (size_t index = 0; index < elements; ++index) {
		const float left = reference[index];
		const float right = candidate[index];
		const bool leftFinite = std::isfinite(left);
		const bool rightFinite = std::isfinite(right);
		metrics.referenceFiniteCount += leftFinite ? 1u : 0u;
		metrics.candidateFiniteCount += rightFinite ? 1u : 0u;
		if (!leftFinite || !rightFinite) {
			continue;
		}

		++metrics.finitePairCount;
		const double leftDouble = static_cast<double>(left);
		const double error = std::fabs(leftDouble - static_cast<double>(right));
		absoluteSum += error;
		errorSquares += error * error;
		referenceSquares += leftDouble * leftDouble;
		if (error > metrics.maximumAbsoluteError) {
			metrics.maximumAbsoluteError = error;
		}
	}

	if (metrics.finitePairCount != 0) {
		const double count = static_cast<double>(metrics.finitePairCount);
		metrics.meanAbsoluteError = absoluteSum / count;
		metrics.rootMeanSquareError = std::sqrt(errorSquares / count);
		const double referenceRms = std::sqrt(referenceSquares / count);
		metrics.relativeRootMeanSquareError = referenceRms > 0.0
			? metrics.rootMeanSquareError / referenceRms : 0.0;
	}
	return true;
}

bool compareInt16Buffers(const int16_t *reference, const int16_t *candidate,
			 size_t elements, Int16BufferErrorMetrics &metrics)
{
	metrics = Int16BufferErrorMetrics{};
	if (!reference || !candidate || elements == 0 ||
	    elements > UINT32_MAX || elements > SIZE_MAX / sizeof(int16_t)) {
		return false;
	}

	metrics.elementCount = static_cast<uint32_t>(elements);
	metrics.referenceCrc32 = crc32(reference, elements * sizeof(int16_t));
	metrics.candidateCrc32 = crc32(candidate, elements * sizeof(int16_t));
	for (size_t index = 0; index < elements; ++index) {
		const int32_t delta = static_cast<int32_t>(candidate[index]) -
				      static_cast<int32_t>(reference[index]);
		const uint32_t magnitude = static_cast<uint32_t>(
			delta < 0 ? -delta : delta);
		if (magnitude == 0) {
			continue;
		}
		++metrics.mismatchedElements;
		if (magnitude > metrics.maximumAbsoluteDelta) {
			metrics.maximumAbsoluteDelta = magnitude;
		}
		if (magnitude == 1) {
			++metrics.deltaOneCount;
		} else if (magnitude == 2) {
			++metrics.deltaTwoCount;
		} else {
			++metrics.deltaGreaterThanTwoCount;
		}
	}
	return true;
}

} // namespace h1diag
