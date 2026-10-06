#include "frontend_diagnostics.hpp"

#include "h1_contract.h"
#include "model_storage.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/kernel.h>

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

namespace {
constexpr int kCompactFrames = H1_M55_FRAME_COUNT;
constexpr int kCompactBins = H1_M55_RFFT_BINS;
constexpr int kCompactBands = H1_M55_MEL_BANDS;
constexpr size_t kCompactValues = kCompactFrames * kCompactBands;

uint32_t crcFloatUpdate(uint32_t crc, uint32_t bits)
{
	for (unsigned byte = 0; byte < 4; ++byte) {
		crc ^= static_cast<uint8_t>(bits >> (byte * 8));
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^
			      (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return crc;
}

struct CompactMetricAccumulator {
	uint32_t finiteReference = 0;
	uint32_t finiteCandidate = 0;
	uint32_t finitePairs = 0;
	uint32_t exact = 0;
	double absoluteSum = 0.0;
	double errorSquares = 0.0;
	double referenceSquares = 0.0;
	double maxAbsolute = 0.0;
	uint32_t referenceCrc = UINT32_C(0xffffffff);
	uint32_t candidateCrc = UINT32_C(0xffffffff);

	void add(float reference, float candidate)
	{
		const uint32_t referenceBits = floatBits(reference);
		const uint32_t candidateBits = floatBits(candidate);
		referenceCrc = crcFloatUpdate(referenceCrc, referenceBits);
		candidateCrc = crcFloatUpdate(candidateCrc, candidateBits);
		const bool leftFinite = std::isfinite(reference);
		const bool rightFinite = std::isfinite(candidate);
		finiteReference += leftFinite ? 1u : 0u;
		finiteCandidate += rightFinite ? 1u : 0u;
		finitePairs += leftFinite && rightFinite ? 1u : 0u;
		if (referenceBits == candidateBits) ++exact;
		if (!leftFinite || !rightFinite) return;
		const double left = static_cast<double>(reference);
		const double right = static_cast<double>(candidate);
		const double error = std::fabs(left - right);
		absoluteSum += error;
		errorSquares += error * error;
		referenceSquares += left * left;
		if (error > maxAbsolute) maxAbsolute = error;
	}

	H1M55CompactScalarMelMetrics finish() const
	{
		H1M55CompactScalarMelMetrics result{};
		result.elementCount = kCompactValues;
		result.finiteReference = finiteReference;
		result.finiteCandidate = finiteCandidate;
		result.finitePairs = finitePairs;
		result.exactBitMatches = exact;
		result.referenceCrc32 = ~referenceCrc;
		result.candidateCrc32 = ~candidateCrc;
		result.maximumAbsoluteError = maxAbsolute;
		if (finitePairs == kCompactValues) {
			const double count = static_cast<double>(kCompactValues);
			result.meanAbsoluteError = absoluteSum / count;
			result.rootMeanSquareError = std::sqrt(errorSquares / count);
			const double referenceRms = std::sqrt(referenceSquares / count);
			result.relativeRootMeanSquareError = referenceRms > 0.0
				? result.rootMeanSquareError / referenceRms : 0.0;
		}
		return result;
	}
};

void compactMelScalarFour(const float *powerGroup,
			  const H1M55CompactMel &compact,
			  float *output, uint32_t firstFrame)
{
	for (uint32_t lane = 0; lane < 4; ++lane) {
		const float *const framePower = powerGroup + lane * kCompactBins;
		for (int band = 0; band < kCompactBands; ++band) {
			const H1M55MelSpan &span = compact.spans[band];
			float sum = 0.0f;
			for (uint32_t i = 0; i < span.length; ++i) {
				sum += framePower[span.startBin + i] *
				       compact.weights[span.weightOffset + i];
			}
			output[(firstFrame + lane) * kCompactBands + band] = sum;
		}
	}
}

} // namespace

bool h1RunCompactScalarMelDiagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	H1M55CompactScalarMelReport &report)
{
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS ||
	    !h1FrontendM55Ready(runtime)) return false;
	const float *mel = nullptr;
	if (!h1GetFrontendMel(&mel)) return false;
	if (h1InitializeReferenceSpectral(scratch) != H1FrontendStatus::Ok)
		return false;
	for (int frame = 0; frame < kCompactFrames; ++frame) {
		if (h1RunReferenceSpectralFrame(waveform, waveformElements, scratch,
					frame) != H1FrontendStatus::Ok) {
			return false;
		}
		for (int band = 0; band < kCompactBands; ++band) {
			float dense = 0.0f;
			for (int bin = 0; bin < kCompactBins; ++bin)
				dense += scratch.power[bin] * mel[band * kCompactBins + bin];
			if (!std::isfinite(dense)) return false;
			scratch.melRaw[static_cast<size_t>(frame) * kCompactBands + band] = dense;
		}
	}

	const H1M55CompactMel &compact = *runtime.compactMel;
	bool structureValid = compact.ready == 1 && compact.weightCount > 0 &&
		compact.weightCount <= H1_M55_MEL_MAX_NONZERO_WEIGHTS;
	bool weightIdentityValid = structureValid;
	uint32_t expectedOffset = 0;
	for (int band = 0; band < kCompactBands; ++band) {
		const float *row = mel + band * kCompactBins;
		int first = 0;
		while (first < kCompactBins && row[first] == 0.0f) ++first;
		int end = kCompactBins;
		while (end > first && row[end - 1] == 0.0f) --end;
		const H1M55MelSpan &span = compact.spans[band];
		const uint32_t length = static_cast<uint32_t>(end - first);
		if (length == 0 || span.startBin != first || span.length != length ||
		    span.weightOffset != expectedOffset ||
		    static_cast<uint32_t>(span.startBin) + span.length > kCompactBins ||
		    static_cast<uint32_t>(span.weightOffset) + span.length > compact.weightCount) {
			structureValid = false;
			weightIdentityValid = false;
			continue;
		}
		for (int bin = 0; bin < kCompactBins; ++bin) {
			if ((bin < first || bin >= end) && row[bin] != 0.0f)
				structureValid = false;
		}
		for (uint32_t index = 0; index < length; ++index) {
			const float denseWeight = row[first + static_cast<int>(index)];
			const float compactWeight = compact.weights[span.weightOffset + index];
			if (denseWeight == 0.0f || !std::isfinite(denseWeight) ||
			    floatBits(denseWeight) != floatBits(compactWeight))
				weightIdentityValid = false;
		}
		expectedOffset += length;
	}
	if (expectedOffset != compact.weightCount) {
		structureValid = false;
		weightIdentityValid = false;
	}
	if (!structureValid || !weightIdentityValid) return false;

	uint32_t densePowerCrc = UINT32_C(0xffffffff);
	uint32_t compactPowerCrc = UINT32_C(0xffffffff);
	uint64_t spectralCycles = 0;
	uint64_t compactMelCycles = 0;
	for (int frame = 0; frame < kCompactFrames; ++frame) {
		H1M55SpectralTiming frameTiming{};
		H1M55SpectralCapture capture{nullptr, nullptr, nullptr, scratch.power,
					     &frameTiming, nullptr};
		const uint64_t spectralStart = k_cycle_get_64();
		const bool spectralOk = h1M55SpectralProcessFrame(
			&runtime.spectral, waveform, waveformElements, runtime.hann,
			frame, &capture, H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, false);
		spectralCycles += k_cycle_get_64() - spectralStart;
		if (!spectralOk) return false;
		for (int bin = 0; bin < kCompactBins; ++bin) {
			if (!std::isfinite(scratch.power[bin])) return false;
			densePowerCrc = crcFloatUpdate(densePowerCrc,
						       floatBits(scratch.power[bin]));
		}
		for (int band = 0; band < kCompactBands; ++band) {
			float dense = 0.0f;
			for (int bin = 0; bin < kCompactBins; ++bin)
				dense += scratch.power[bin] * mel[band * kCompactBins + bin];
			if (!std::isfinite(dense)) return false;
			scratch.gray[static_cast<size_t>(frame) * kCompactBands + band] = dense;
		}
		for (int bin = 0; bin < kCompactBins; ++bin)
			compactPowerCrc = crcFloatUpdate(compactPowerCrc,
							floatBits(scratch.power[bin]));
		const uint64_t melStart = k_cycle_get_64();
		for (int band = 0; band < kCompactBands; ++band) {
			const H1M55MelSpan &span = compact.spans[band];
			float sum = 0.0f;
			for (uint32_t index = 0; index < span.length; ++index)
				sum += scratch.power[span.startBin + index] *
				       compact.weights[span.weightOffset + index];
			if (!std::isfinite(sum)) return false;
			scratch.melDb[static_cast<size_t>(frame) * kCompactBands + band] = sum;
		}
		compactMelCycles += k_cycle_get_64() - melStart;
	}

	CompactMetricAccumulator isolation;
	CompactMetricAccumulator endToEnd;
	for (size_t index = 0; index < kCompactValues; ++index) {
		isolation.add(scratch.gray[index], scratch.melDb[index]);
		endToEnd.add(scratch.melRaw[index], scratch.melDb[index]);
	}
	report = H1M55CompactScalarMelReport{};
	report.frameCount = kCompactFrames;
	report.melBins = kCompactBands;
	report.valueCount = kCompactValues;
	report.compactWeightCount = compact.weightCount;
	report.compactStructureValid = structureValid;
	report.compactWeightIdentityValid = weightIdentityValid;
	report.clockHz = sys_clock_hw_cycles_per_sec();
	report.denseNativePowerCrc32 = ~densePowerCrc;
	report.compactNativePowerCrc32 = ~compactPowerCrc;
	report.isolation = isolation.finish();
	report.endToEnd = endToEnd.finish();
	report.spectralCycles = spectralCycles;
	report.compactMelCycles = compactMelCycles;
	report.totalCycles = spectralCycles + compactMelCycles;
	return report.clockHz != 0;
}

bool h1RunMveCompactMelDiagnostic(
	const float *waveform, size_t waveformElements,
	H1FrontendScratch &scratch, H1M55FrontendRuntime &runtime,
	H1M55MveCompactMelReport &report)
{
	if (!waveform || waveformElements != H1_WAVEFORM_ELEMENTS ||
	    !h1FrontendM55Ready(runtime)) return false;
	H1M55CompactScalarMelReport compactCheck{};
	if (!h1RunCompactScalarMelDiagnostic(waveform, waveformElements, scratch,
					      runtime, compactCheck) ||
	    !compactCheck.compactStructureValid ||
	    !compactCheck.compactWeightIdentityValid ||
	    compactCheck.compactWeightCount != 1885u) return false;

	const H1M55CompactMel &compact = *runtime.compactMel;
	constexpr size_t kGroupPowerOffset = 30000;
	float *const scalarOutput = scratch.gray;
	float *const groupPower = scratch.gray + kGroupPowerOffset;
	static_assert(kGroupPowerOffset + 4 * kCompactBins < 224 * 281);
	uint32_t powerCrc = UINT32_C(0xffffffff);
	uint64_t scalarCycles = 0;
	uint64_t mveCycles = 0;
	bool usedMve = true;
	for (int firstFrame = 0; firstFrame < kCompactFrames; firstFrame += 4) {
		for (int lane = 0; lane < 4; ++lane) {
			H1M55SpectralTiming frameTiming{};
			H1M55SpectralCapture capture{nullptr, nullptr, nullptr, scratch.power,
						     &frameTiming, nullptr};
			if (!h1M55SpectralProcessFrame(
				    &runtime.spectral, waveform, waveformElements, runtime.hann,
				    firstFrame + lane, &capture,
				    H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, false)) return false;
			float *const framePower = groupPower + lane * kCompactBins;
			std::memcpy(framePower, scratch.power, kCompactBins * sizeof(float));
			for (int bin = 0; bin < kCompactBins; ++bin) {
				if (!std::isfinite(framePower[bin])) return false;
				powerCrc = crcFloatUpdate(powerCrc, floatBits(framePower[bin]));
			}
		}
		const uint64_t scalarStart = k_cycle_get_64();
		compactMelScalarFour(groupPower, compact, scalarOutput,
				     static_cast<uint32_t>(firstFrame));
		scalarCycles += k_cycle_get_64() - scalarStart;
		const uint64_t mveStart = k_cycle_get_64();
		bool frameUsedMve = false;
		if (!h1M55CompactMelFourFrames(
			groupPower, compact, scratch.melDb,
			static_cast<uint32_t>(firstFrame), frameUsedMve)) {
			return false;
		}
		usedMve = frameUsedMve && usedMve;
		mveCycles += k_cycle_get_64() - mveStart;
	}

	CompactMetricAccumulator isolated;
	CompactMetricAccumulator contextual;
	uint32_t maximumIndex = UINT32_MAX;
	double maximumError = -1.0;
	for (size_t index = 0; index < kCompactValues; ++index) {
		const float scalar = scalarOutput[index];
		const float mve = scratch.melDb[index];
		const float reference = scratch.melRaw[index];
		if (!std::isfinite(scalar) || !std::isfinite(mve) ||
		    !std::isfinite(reference)) return false;
		isolated.add(scalar, mve);
		contextual.add(reference, mve);
		const double error = std::fabs(static_cast<double>(scalar) -
					       static_cast<double>(mve));
		if (error > maximumError) {
			maximumError = error;
			maximumIndex = static_cast<uint32_t>(index);
		}
	}
	const uint32_t observedPowerCrc = ~powerCrc;
	if (observedPowerCrc != compactCheck.denseNativePowerCrc32) return false;
	report = H1M55MveCompactMelReport{};
	report.scalarVsMve = isolated.finish();
	report.referenceVsMve = contextual.finish();
	report.nativePowerCrc32 = observedPowerCrc;
	report.referencePowerCrc32 = compactCheck.denseNativePowerCrc32;
	report.maximumErrorIndex = maximumIndex;
	if (maximumIndex != UINT32_MAX) {
		report.maximumErrorFrame = maximumIndex / kCompactBands;
		report.maximumErrorBand = maximumIndex % kCompactBands;
		report.maximumErrorScalarValue = scalarOutput[maximumIndex];
		report.maximumErrorMveValue = scratch.melDb[maximumIndex];
	}
	report.clockHz = sys_clock_hw_cycles_per_sec();
	report.usedMve = usedMve ? 1u : 0u;
	report.scalarCycles = scalarCycles;
	report.mveCycles = mveCycles;
	report.frames = kCompactFrames;
	report.bands = kCompactBands;
	report.retainedWeights = compact.weightCount;
	return report.clockHz != 0;
}

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

	comparison.referenceStatus = h1RunFrontendLegacy(
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
