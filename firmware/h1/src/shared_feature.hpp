// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace birdnet::h1 {

enum class TensorDType : uint8_t {
	Int16,
};

struct TensorShape4D {
	uint32_t dimensions[4];
};

struct SharedFeatureDescriptor {
	const void *data;
	TensorShape4D shape;
	TensorDType dtype;
	float scale;
	int32_t zeroPoint;
	size_t byteLength;
};

enum class ExecutionMode : uint8_t {
	H1_ONLY,
	H23_ONLY,
	H1_H23_FULL,
};

enum SharedFeatureConsumer : uint32_t {
	H1_GEM_CONSUMER = UINT32_C(1) << 0,
	H23_CONSUMER = UINT32_C(1) << 1,
};

constexpr uint32_t consumerMask(ExecutionMode mode)
{
	switch (mode) {
	case ExecutionMode::H1_ONLY:
		return H1_GEM_CONSUMER;
	case ExecutionMode::H23_ONLY:
		return H23_CONSUMER;
	case ExecutionMode::H1_H23_FULL:
		return H1_GEM_CONSUMER | H23_CONSUMER;
	}
	return 0;
}

struct SharedFeatureLease {
	SharedFeatureDescriptor feature;
	ExecutionMode mode;
	uint32_t completedConsumers;
};

constexpr uint32_t pendingConsumers(const SharedFeatureLease &lease)
{
	return consumerMask(lease.mode) & ~lease.completedConsumers;
}

inline bool completeConsumer(SharedFeatureLease &lease, uint32_t consumer)
{
	const uint32_t enabled = consumerMask(lease.mode);
	if ((consumer & enabled) == 0 || (consumer & (consumer - 1)) != 0) {
		return false;
	}
	lease.completedConsumers |= consumer;
	return true;
}

constexpr bool mayRelease(const SharedFeatureLease &lease)
{
	return pendingConsumers(lease) == 0;
}

} // namespace birdnet::h1
