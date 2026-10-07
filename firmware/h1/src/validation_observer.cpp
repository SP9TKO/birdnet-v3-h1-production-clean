// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "validation_observer.hpp"
#include "h1_contract.h"
#include "runtime_profile.h"
#include <cstring>

H1ValidationObserverState h1ValidationObserverState
	__attribute__((section(".h1_validation_observer"), aligned(32)));

static_assert(H1V_PRIMARY_END == 32 && H1V_MARK_COUNT == 33, "frozen marker ABI");
static_assert(sizeof(H1ValidationObservation) == 344, "validation sample ABI");
static_assert(sizeof(H1ValidationObserverState) == 7240, "validation storage ABI");

namespace {
constexpr uint32_t kBoundaryBytes[7] = {
	H1_FRONTEND_BYTES, H1_BACKBONE_INPUT_BYTES, H1_SHARED_FEATURE_BYTES,
	H1_EMBEDDING_BYTES, H1_CLASSIFIER_INPUT_BYTES, H1_LOGIT_BYTES, H1_SCORE_BYTES,
};
}

void h1ValidationObserverInit()
{
	std::memset(&h1ValidationObserverState, 0, sizeof(h1ValidationObserverState));
}

void h1ValidationObserverCampaignBegin(uint32_t sequence)
{
	h1ValidationObserverInit();
	h1ValidationObserverState.campaignSequence = sequence;
}

void h1ValidationObserverPrepare(bool active)
{
	h1ValidationObserverState.active = active;
	if (active) {
		std::memset(&h1ValidationObserverState.last, 0,
			    sizeof(h1ValidationObserverState.last));
	}
}

void h1ValidationObserverAt(H1ValidationMark marker, uint64_t cycles)
{
	if (h1ValidationObserverState.active) {
		h1ValidationObserverState.last.timestamps[marker] = cycles;
	}
}

void h1ValidationObserverMark(H1ValidationMark marker)
{
	if (!h1ValidationObserverState.active) {
		return;
	}
	auto &o = h1ValidationObserverState.last;
	if (marker == H1V_WAVEFORM_CRC_BEGIN) {
		++o.waveformCalls;
		o.waveformBytes += H1_WAVEFORM_BYTES;
	} else if (marker >= H1V_BOUNDARY_0_BEGIN && marker <= H1V_BOUNDARY_6_BEGIN &&
		   (marker - H1V_BOUNDARY_0_BEGIN) % 2 == 0) {
		const uint32_t index = (marker - H1V_BOUNDARY_0_BEGIN) / 2;
		++o.boundaryCalls[index];
		o.boundaryBytes[index] += kBoundaryBytes[index];
	}
	o.timestamps[marker] = h1ProfileNow();
}

void h1ValidationObserverCapture(int32_t index, uint64_t end,
				 uint32_t runSequence, uint32_t clockHz,
				 bool success)
{
	if (!h1ValidationObserverState.active) {
		return;
	}
	auto &o = h1ValidationObserverState.last;
	o.timestamps[H1V_PRIMARY_END] = end;
	o.runSequence = runSequence;
	o.clockHz = clockHz;
	if (!success || clockHz != 400000000u || o.waveformCalls != 1 ||
	    o.waveformBytes != H1_WAVEFORM_BYTES) {
		o.error = 1;
	}
	for (uint32_t i = 0; i < H1V_MARK_COUNT; ++i) {
		if (o.timestamps[i] == 0 ||
		    (i > 0 && o.timestamps[i] < o.timestamps[i - 1])) {
			o.error = 2;
		}
	}
	for (uint32_t i = 0; i < 7; ++i) {
		if (o.boundaryCalls[i] != 1 || o.boundaryBytes[i] != kBoundaryBytes[i]) {
			o.error = 3;
		}
	}
	o.valid = o.error == 0;
	if (index >= 0 && index < 20) {
		h1ValidationObserverState.measured[index] = o;
		h1ValidationObserverState.measuredStored = uint32_t(index) + 1;
	}
	h1ValidationObserverState.active = 0;
}
