// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "postprocessing_observer.hpp"
#include <cstring>

H1PostprocessObserverState h1PostprocessObserverState
	__attribute__((section(".h1_postprocess_observer"), aligned(32)));

static_assert(sizeof(H1PostprocessObservation) == 128, "observer sample ABI");
static_assert(sizeof(H1PostprocessObserverState) <= 4096, "observer storage budget");

void h1PostprocessObserverInit()
{
	std::memset(&h1PostprocessObserverState, 0, sizeof(h1PostprocessObserverState));
}

void h1PostprocessObserverPrepare()
{
	std::memset(&h1PostprocessObserverState.last, 0,
		    sizeof(h1PostprocessObserverState.last));
}

void h1PostprocessObserverFinalize(uint64_t start, uint64_t end,
				 uint32_t runSequence, uint32_t clockHz)
{
	auto &o = h1PostprocessObserverState.last;
	o.totalStart = start;
	o.totalEnd = end;
	o.runSequence = runSequence;
	o.clockHz = clockHz;
	if (!(start <= o.scoreStart && o.scoreStart < o.scoreEnd &&
	      o.scoreEnd <= o.topStart && o.topStart < o.topEnd &&
	      o.topEnd < o.materialEnd && o.materialEnd <= end)) {
		o.error = 1;
		return;
	}
	o.totalCycles = end - start;
	o.scoreCycles = o.scoreEnd - o.scoreStart;
	o.topCycles = o.topEnd - o.topStart;
	o.materialCycles = o.materialEnd - o.topEnd;
	const uint64_t classified = o.scoreCycles + o.topCycles + o.materialCycles;
	if (classified > o.totalCycles || clockHz != 400000000u || o.selected != 100u) {
		o.error = 2;
		return;
	}
	o.residualCycles = o.totalCycles - classified;
	o.valid = 1;
}

void h1PostprocessObserverCampaignBegin(uint32_t sequence)
{
	std::memset(h1PostprocessObserverState.measured, 0,
		    sizeof(h1PostprocessObserverState.measured));
	h1PostprocessObserverState.campaignSequence = sequence;
	h1PostprocessObserverState.measuredStored = 0;
}

void h1PostprocessObserverCapture(int32_t index)
{
	if (index < 0 || index >= 20) {
		return;
	}
	h1PostprocessObserverState.measured[index] = h1PostprocessObserverState.last;
	h1PostprocessObserverState.measuredStored = uint32_t(index) + 1;
}
