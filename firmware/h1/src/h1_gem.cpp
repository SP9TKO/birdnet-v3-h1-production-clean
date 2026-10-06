// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "h1_gem.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace birdnet::h1 {
namespace {
constexpr uint32_t kBatch = 1;
constexpr uint32_t kHeight = 7;
constexpr uint32_t kWidth = 9;
constexpr uint32_t kChannels = 1280;
constexpr size_t kInputElements = size_t{kBatch} * kHeight * kWidth * kChannels;
constexpr size_t kInputBytes = kInputElements * sizeof(int16_t);
constexpr float kClampEpsilon = 1.0e-6f;
constexpr float kPower = 7.408084869384766f;
constexpr float kReciprocalPower = 0.13498765230178833f;
constexpr float kSpatialElements = 63.0f;

uint32_t floatBits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

bool validDescriptor(const SharedFeatureDescriptor &feature)
{
	return feature.data != nullptr && feature.dtype == TensorDType::Int16 &&
	       feature.shape.dimensions[0] == kBatch &&
	       feature.shape.dimensions[1] == kHeight &&
	       feature.shape.dimensions[2] == kWidth &&
	       feature.shape.dimensions[3] == kChannels &&
	       feature.byteLength == kInputBytes && feature.zeroPoint == 0 &&
	       floatBits(feature.scale) == UINT32_C(0x38606d52) &&
	       floatBits(kClampEpsilon) == UINT32_C(0x358637bd) &&
	       floatBits(kPower) == UINT32_C(0x40ed0f08) &&
	       floatBits(kReciprocalPower) == UINT32_C(0x3e0a3a34);
}
} // namespace

GemStatus runGem(const SharedFeatureDescriptor &feature, float *output,
		 size_t outputElements)
{
	static_assert(sizeof(float) == 4, "H1 requires IEEE binary32 storage");
	static_assert(sizeof(int16_t) == 2, "H1 requires 16-bit input storage");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "H1 feature feature is little-endian"
#endif
	if (!validDescriptor(feature)) {
		return GemStatus::InvalidDescriptor;
	}
	if (output == nullptr || outputElements != kGemOutputElements) {
		return GemStatus::InvalidOutput;
	}

	const auto *input = static_cast<const int16_t *>(feature.data);
	for (size_t channel = 0; channel < kChannels; ++channel) {
		float sum = 0.0f;
		for (size_t height = 0; height < kHeight; ++height) {
			for (size_t width = 0; width < kWidth; ++width) {
				const size_t index = (height * kWidth + width) * kChannels + channel;
				const int32_t centered = int32_t(input[index]) - feature.zeroPoint;
				const float dequantized = static_cast<float>(centered) * feature.scale;
				const float clamped = dequantized > kClampEpsilon
							      ? dequantized
							      : kClampEpsilon;
				sum += ::powf(clamped, kPower);
			}
		}
		output[channel] = ::powf(sum / kSpatialElements, kReciprocalPower);
		if (!std::isfinite(output[channel])) {
			return GemStatus::NonFiniteOutput;
		}
	}
	return GemStatus::Ok;
}

} // namespace birdnet::h1
