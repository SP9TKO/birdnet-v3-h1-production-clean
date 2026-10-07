// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include "topk_heap.hpp"

// Diagnostic storage only. Existing runtime/result structures remain unchanged.
struct H1PostprocessObservation {
	uint64_t scoreStart, scoreEnd, topStart, topEnd, materialEnd;
	uint64_t totalStart, totalEnd;
	// Cycle deltas are derived from the unchanged timestamps by command 151.
	// Reuse their 40-byte storage for heap counts without growing this ABI.
	H1TopkHeapCounts heap;
	uint64_t reserved;
	uint32_t comparisons, shifts, selected;
	uint32_t runSequence, clockHz, valid, error;
};

struct H1PostprocessObserverState {
	H1PostprocessObservation last;
	H1PostprocessObservation measured[20];
	uint32_t campaignSequence, measuredStored;
};

extern H1PostprocessObserverState h1PostprocessObserverState;
void h1PostprocessObserverInit();
void h1PostprocessObserverPrepare();
void h1PostprocessObserverFinalize(uint64_t start, uint64_t end,
				 uint32_t runSequence, uint32_t clockHz);
void h1PostprocessObserverCampaignBegin(uint32_t sequence);
void h1PostprocessObserverCapture(int32_t index);
