#pragma once

#include <cstddef>
#include <cstdint>

#define H1_BRIDGE_POLICY_SHA256 \
  "67010defc48ed1772ee975b9dd3fc53213198f0b0deca26964e7af579af80087"

namespace birdnet::h1 {

// Exact finite binary32 ratio, nearest-even, then INT16 saturation. Invalid
// (nonfinite source or nonpositive/nonfinite scale) inputs return false without
// defining a quantized code. Saturation is -1, 0, or +1 after rounding.
bool quantizeClassifierInputBits(uint32_t sourceBits, uint32_t scaleBits,
				 int16_t &output, int8_t &saturation);

// Ordinary production bridge: frozen classifier scale and zero point 0.
bool quantizeClassifierInput(const float *input, int16_t *output, size_t elements,
			   size_t &saturatedLow, size_t &saturatedHigh);

} // namespace birdnet::h1
