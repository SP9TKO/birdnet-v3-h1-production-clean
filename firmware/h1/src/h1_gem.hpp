#pragma once

#include "shared_feature.hpp"

#include <cstddef>
#include <cstdint>

namespace birdnet::h1 {

constexpr size_t kGemOutputElements = 1280;

enum class GemStatus : uint8_t {
	Ok,
	InvalidDescriptor,
	InvalidOutput,
	NonFiniteOutput,
};

GemStatus runGem(const SharedFeatureDescriptor &feature, float *output,
		 size_t outputElements);

} // namespace birdnet::h1
