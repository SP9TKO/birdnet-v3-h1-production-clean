// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "classifier_bridge.hpp"
#include "h1_contract.h"

#include <cstring>
#include <limits>

namespace birdnet::h1 {
namespace {
void saturated(bool negative, int16_t &output, int8_t &saturation)
{
	output = negative ? INT16_MIN : INT16_MAX;
	saturation = negative ? -1 : 1;
}
} // namespace

bool quantizeClassifierInputBits(uint32_t sourceBits, uint32_t scaleBits,
				 int16_t &output, int8_t &saturation)
{
	const uint32_t sourceExponent = (sourceBits >> 23) & 255u;
	const uint32_t scaleExponent = (scaleBits >> 23) & 255u;
	if (sourceExponent == 255u || scaleExponent == 255u ||
	    (scaleBits >> 31) != 0u || (scaleBits & UINT32_C(0x7fffffff)) == 0u) {
		return false;
	}
	const bool negative = (sourceBits >> 31) != 0u;
	uint64_t numerator = sourceBits & UINT32_C(0x007fffff);
	uint64_t denominator = scaleBits & UINT32_C(0x007fffff);
	if (sourceExponent != 0u) {
		numerator |= UINT64_C(0x00800000);
	}
	if (scaleExponent != 0u) {
		denominator |= UINT64_C(0x00800000);
	}
	if (numerator == 0u) {
		output = 0; // Both signs of zero.
		saturation = 0;
		return true;
	}
	const int32_t sourcePower = sourceExponent == 0u
		? -149 : static_cast<int32_t>(sourceExponent) - 150;
	const int32_t scalePower = scaleExponent == 0u
		? -149 : static_cast<int32_t>(scaleExponent) - 150;
	const int32_t power = sourcePower - scalePower;
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	if (power >= 0) {
		const uint32_t shift = static_cast<uint32_t>(power);
		if (shift >= 64u || numerator > (maximum >> shift)) {
			// Exact numerator >= 2^64, denominator < 2^24:
			// exact quotient > 2^40. Its RNE result necessarily saturates.
			saturated(negative, output, saturation);
			return true;
		}
		numerator <<= shift;
	} else {
		const uint32_t shift = static_cast<uint32_t>(-power);
		if (shift >= 64u || denominator > (maximum >> shift)) {
			// Exact denominator >= 2^64, numerator < 2^24:
			// exact quotient < 2^-40, so its RNE result is exactly zero.
			output = 0;
			saturation = 0;
			return true;
		}
		denominator <<= shift;
	}
	uint64_t rounded = numerator / denominator;
	const uint64_t remainder = numerator % denominator;
	// Compare 2*remainder to denominator without overflowing either integer.
	const uint64_t complement = denominator - remainder;
	if (remainder > complement ||
	    (remainder == complement && (rounded & 1u) != 0u)) {
		if (rounded == maximum) {
			saturated(negative, output, saturation);
			return true;
		}
		++rounded;
	}
	const uint64_t limit = negative ? UINT64_C(32768) : UINT64_C(32767);
	if (rounded > limit) {
		saturated(negative, output, saturation);
	} else {
		const int32_t magnitude = static_cast<int32_t>(rounded);
		output = static_cast<int16_t>(negative ? -magnitude : magnitude);
		saturation = 0;
	}
	return true;
}

bool quantizeClassifierInput(const float *input, int16_t *output, size_t elements,
			   size_t &saturatedLow, size_t &saturatedHigh)
{
	static_assert(sizeof(float) == sizeof(uint32_t) &&
		      std::numeric_limits<float>::is_iec559,
		      "H1 bridge requires IEEE-754 binary32 storage");
	static_assert(H1_QUANT_ZERO_POINT == 0, "H1 bridge requires zero point 0");
	if (input == nullptr || output == nullptr) {
		return false;
	}
	saturatedLow = 0;
	saturatedHigh = 0;
	for (size_t index = 0; index < elements; ++index) {
		uint32_t bits;
		std::memcpy(&bits, &input[index], sizeof(bits));
		int8_t saturation;
		if (!quantizeClassifierInputBits(bits, H1_CLASSIFIER_INPUT_SCALE_BITS,
					 output[index], saturation)) {
			return false;
		}
		saturatedLow += saturation < 0;
		saturatedHigh += saturation > 0;
	}
	return true;
}

} // namespace birdnet::h1
