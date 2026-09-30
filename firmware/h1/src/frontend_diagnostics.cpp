#include "frontend_diagnostics.hpp"

#include "frontend_storage.hpp"
#include "h1_contract.h"
#include "model_storage.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr size_t kFrontendBytes = H1_FRONTEND_ELEMENTS * sizeof(float);
constexpr size_t kGuardBytes = 32;
static_assert(H1_FRONTEND_ELEMENTS == 1u * 224u * 281u * 3u);
static_assert(kFrontendBytes == H1_FRONTEND_BYTES);
static_assert(alignof(H1Boundaries) >= 32u);
static_assert(offsetof(H1Boundaries, backboneInput) == H1_FRONTEND_BYTES);

uint32_t crc32(const void *data, size_t bytes)
{
	const auto *const raw = static_cast<const uint8_t *>(data);
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t index = 0; index < bytes; ++index) {
		crc ^= raw[index];
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

uint32_t floatBits(float value)
{
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

uint32_t orderedFloatBits(uint32_t bits)
{
	return (bits & UINT32_C(0x80000000)) ? ~bits : (bits | UINT32_C(0x80000000));
}

uint32_t ulpDistance(uint32_t left, uint32_t right)
{
	const uint32_t a = orderedFloatBits(left);
	const uint32_t b = orderedFloatBits(right);
	return a >= b ? a - b : b - a;
}

bool rangeValid(const void *pointer, size_t bytes)
{
	const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
	return pointer && bytes != 0 && start <= UINTPTR_MAX - bytes;
}

bool rangesDisjoint(const void *left, size_t leftBytes,
		    const void *right, size_t rightBytes)
{
	if (!rangeValid(left, leftBytes) || !rangeValid(right, rightBytes)) {
		return false;
	}
	const uintptr_t a = reinterpret_cast<uintptr_t>(left);
	const uintptr_t b = reinterpret_cast<uintptr_t>(right);
	return a + leftBytes <= b || b + rightBytes <= a;
}

bool outputRangesSafe(const float *waveform, size_t waveformElements,
		      H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
		      const float *referenceOutput, const float *productionOutput)
{
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS ||
	    !referenceOutput || !productionOutput ||
	    (reinterpret_cast<uintptr_t>(referenceOutput) & 31u) != 0u ||
	    (reinterpret_cast<uintptr_t>(productionOutput) & 31u) != 0u) {
		return false;
	}
	const void *const outputs[] = {referenceOutput, productionOutput};
	const void *const protectedRanges[] = {
		waveform,
		&scratch,
		&runtime,
		runtime.compactMel,
		&h1M55CompactMel(),
		&h1M55SpectralWorkspace(),
		h1FrontendModelData,
	};
	const size_t protectedBytes[] = {
		waveformElements * sizeof(float),
		sizeof(scratch),
		sizeof(runtime),
		runtime.compactMel ? sizeof(*runtime.compactMel) : 0,
		sizeof(h1M55CompactMel()),
		sizeof(h1M55SpectralWorkspace()),
		H1_FRONTEND_MODEL_BYTES,
	};
	for (size_t index = 0; index < 2; ++index) {
		if (!rangeValid(outputs[index], kFrontendBytes)) {
			return false;
		}
		for (size_t other = index + 1; other < 2; ++other) {
			if (!rangesDisjoint(outputs[index], kFrontendBytes,
					    outputs[other], kFrontendBytes)) {
				return false;
			}
		}
		for (size_t other = 0;
		     other < sizeof(protectedRanges) / sizeof(protectedRanges[0]); ++other) {
			if (!protectedRanges[other] || protectedBytes[other] == 0 ||
			    !rangesDisjoint(outputs[index], kFrontendBytes,
					    protectedRanges[other], protectedBytes[other])) {
				return false;
			}
		}
	}
	return true;
}

struct BufferGuard {
	const uint8_t *beforeAddress;
	const uint8_t *afterAddress;
	uint8_t before[kGuardBytes];
	uint8_t after[kGuardBytes];
};

bool captureGuards(const float *output, BufferGuard &guard)
{
	const uintptr_t begin = reinterpret_cast<uintptr_t>(output);
	const uintptr_t end = begin + kFrontendBytes;
	if (begin < kGuardBytes || end > UINTPTR_MAX - kGuardBytes) {
		return false;
	}
	guard.beforeAddress = reinterpret_cast<const uint8_t *>(begin - kGuardBytes);
	guard.afterAddress = reinterpret_cast<const uint8_t *>(end);
	std::memcpy(guard.before, guard.beforeAddress, kGuardBytes);
	std::memcpy(guard.after, guard.afterAddress, kGuardBytes);
	return true;
}

bool guardsUnchanged(const BufferGuard &guard)
{
	return std::memcmp(guard.before, guard.beforeAddress, kGuardBytes) == 0 &&
	       std::memcmp(guard.after, guard.afterAddress, kGuardBytes) == 0;
}

uint32_t finiteCount(const float *values)
{
	uint32_t count = 0;
	for (size_t index = 0; index < H1_FRONTEND_ELEMENTS; ++index) {
		count += std::isfinite(values[index]) ? 1u : 0u;
	}
	return count;
}

void compareOutputs(const float *reference, const float *production,
		    H1M55FrontendComparison &result)
{
	double absoluteSum = 0.0;
	double errorSquares = 0.0;
	double referenceSquares = 0.0;
	double productionSquares = 0.0;
	double dot = 0.0;
	result.firstMismatchIndex = UINT32_MAX;
	for (size_t index = 0; index < H1_FRONTEND_ELEMENTS; ++index) {
		const float a = reference[index];
		const float b = production[index];
		const uint32_t aBits = floatBits(a);
		const uint32_t bBits = floatBits(b);
		if (aBits == bBits) {
			++result.exactBitMatches;
		} else {
			if (result.firstMismatchIndex == UINT32_MAX) {
				result.firstMismatchIndex = static_cast<uint32_t>(index);
			}
			++result.mismatchCount;
			const uint32_t ulp = ulpDistance(aBits, bBits);
			if (ulp > result.maximumUlpDifference) {
				result.maximumUlpDifference = ulp;
			}
		}
		if (!std::isfinite(a) || !std::isfinite(b)) {
			continue;
		}
		const double da = static_cast<double>(a);
		const double db = static_cast<double>(b);
		const double error = std::fabs(da - db);
		absoluteSum += error;
		errorSquares += error * error;
		referenceSquares += da * da;
		productionSquares += db * db;
		dot += da * db;
		if (error > result.maximumAbsoluteError) {
			result.maximumAbsoluteError = error;
		}
	}
	if (result.referenceFiniteCount == H1_FRONTEND_ELEMENTS &&
	    result.productionFiniteCount == H1_FRONTEND_ELEMENTS) {
		const double count = static_cast<double>(H1_FRONTEND_ELEMENTS);
		result.meanAbsoluteError = absoluteSum / count;
		result.rootMeanSquareError = std::sqrt(errorSquares / count);
		const double referenceRms = std::sqrt(referenceSquares / count);
		result.relativeRootMeanSquareError = referenceRms > 0.0
			? result.rootMeanSquareError / referenceRms : 0.0;
		const double denominator = std::sqrt(referenceSquares * productionSquares);
		result.cosineSimilarity = denominator > 0.0 ? dot / denominator : 1.0;
	}
}

void compareRepeat(const float *first, const float *second,
		   H1M55FrontendComparison &result)
{
	result.repeatFirstMismatchIndex = UINT32_MAX;
	for (size_t index = 0; index < H1_FRONTEND_ELEMENTS; ++index) {
		if (floatBits(first[index]) == floatBits(second[index])) {
			++result.repeatExactBitMatches;
		} else {
			if (result.repeatFirstMismatchIndex == UINT32_MAX) {
				result.repeatFirstMismatchIndex = static_cast<uint32_t>(index);
			}
			++result.repeatMismatchCount;
		}
	}
}
} // namespace

H1FrontendStatus h1RunFrontendM55Diagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	float *output, size_t outputElements,
	H1M55FrontendTiming &timing, H1M55FrontendExecution &execution)
{
	return h1RunFrontendM55(waveform, waveformElements, scratch, runtime,
				output, outputElements, timing, execution,
				true, true);
}

bool h1CompareFrontendsM55Diagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	float *referenceOutput, float *productionOutput,
	H1M55FrontendComparison &comparison)
{
	comparison = H1M55FrontendComparison{};
	comparison.firstMismatchIndex = UINT32_MAX;
	comparison.repeatFirstMismatchIndex = UINT32_MAX;
	comparison.referenceStatus = H1FrontendStatus::InvalidInput;
	comparison.productionStatus = H1FrontendStatus::InvalidInput;
	comparison.repeatStatus = H1FrontendStatus::InvalidInput;
	comparison.productionExecution = H1M55FrontendExecution{
		H1M55FrontendState::Idle, H1M55FrontendStage::None,
		H1M55FrontendStage::None, 0};
	comparison.repeatExecution = comparison.productionExecution;
	comparison.bufferRangesSafe = outputRangesSafe(
		waveform, waveformElements, scratch, runtime,
		referenceOutput, productionOutput);
	if (!comparison.bufferRangesSafe) {
		return false;
	}

	BufferGuard referenceGuard{};
	BufferGuard productionGuard{};
	if (!captureGuards(referenceOutput, referenceGuard) ||
	    !captureGuards(productionOutput, productionGuard)) {
		comparison.bufferRangesSafe = false;
		return false;
	}

	comparison.referenceStatus = h1RunFrontend(
		waveform, waveformElements, scratch, referenceOutput,
		H1_FRONTEND_ELEMENTS);
	comparison.productionStatus = h1RunFrontendM55Diagnostic(
		waveform, waveformElements, scratch, runtime, productionOutput,
		H1_FRONTEND_ELEMENTS, comparison.productionTiming,
		comparison.productionExecution);
	if (comparison.referenceStatus == H1FrontendStatus::Ok) {
		comparison.referenceCrc32 = crc32(referenceOutput, kFrontendBytes);
		comparison.referenceFiniteCount = finiteCount(referenceOutput);
	}
	if (comparison.productionStatus == H1FrontendStatus::Ok) {
		comparison.productionCrc32 = crc32(productionOutput, kFrontendBytes);
		comparison.productionFiniteCount = finiteCount(productionOutput);
	}
	if (comparison.referenceStatus == H1FrontendStatus::Ok &&
	    comparison.productionStatus == H1FrontendStatus::Ok) {
		compareOutputs(referenceOutput, productionOutput, comparison);
		comparison.metricsAvailable =
			comparison.referenceFiniteCount == H1_FRONTEND_ELEMENTS &&
			comparison.productionFiniteCount == H1_FRONTEND_ELEMENTS;
	}

	if (comparison.productionStatus == H1FrontendStatus::Ok) {
		H1M55FrontendTiming repeatTiming{};
		comparison.repeatStatus = h1RunFrontendM55Diagnostic(
			waveform, waveformElements, scratch, runtime, referenceOutput,
			H1_FRONTEND_ELEMENTS, repeatTiming, comparison.repeatExecution);
		if (comparison.repeatStatus == H1FrontendStatus::Ok) {
			comparison.repeatCrc32 = crc32(referenceOutput, kFrontendBytes);
			comparison.repeatFiniteCount = finiteCount(referenceOutput);
			compareRepeat(productionOutput, referenceOutput, comparison);
		}
	}

	comparison.guardBytesUnchanged = guardsUnchanged(referenceGuard) &&
		guardsUnchanged(productionGuard);
	return true;
}
