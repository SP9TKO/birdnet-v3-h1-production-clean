// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// Indices are frozen by HOT_PATH_VALIDATION_OBSERVATION_PLAN_V1.json.
enum H1ValidationMark : uint32_t {
	H1V_PRIMARY_START, H1V_RUN_ENTER, H1V_WAVEFORM_CRC_BEGIN,
	H1V_WAVEFORM_CRC_END, H1V_IDENTITY_COPY_END, H1V_IDENTITY_CHECK_END,
	H1V_FRONTEND_WORK_BEGIN, H1V_P0_END, H1V_PROFILE_FINALIZE_END,
	H1V_BOUNDARY_0_BEGIN, H1V_BOUNDARY_0_END,
	H1V_BOUNDARY_1_BEGIN, H1V_BOUNDARY_1_END,
	H1V_BOUNDARY_2_BEGIN, H1V_BOUNDARY_2_END,
	H1V_BOUNDARY_3_BEGIN, H1V_BOUNDARY_3_END,
	H1V_BOUNDARY_4_BEGIN, H1V_BOUNDARY_4_END,
	H1V_BOUNDARY_5_BEGIN, H1V_BOUNDARY_5_END,
	H1V_BOUNDARY_6_BEGIN, H1V_BOUNDARY_6_END,
	H1V_SCORE_ALIAS_END, H1V_RESULT_STATE_BEGIN, H1V_RESULT_STATE_END,
	H1V_PUBLICATION_END, H1V_COMPARE_BEGIN, H1V_COMPARE_END,
	H1V_SAVE_COPY_END, H1V_RESULT_READY_BEGIN, H1V_RESULT_READY_END,
	H1V_PRIMARY_END, H1V_MARK_COUNT,
};

#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
struct H1ValidationObservation {
	uint64_t timestamps[H1V_MARK_COUNT];
	uint32_t waveformBytes, waveformCalls;
	uint32_t boundaryBytes[7], boundaryCalls[7];
	uint32_t runSequence, clockHz, valid, error;
};

struct H1ValidationObserverState {
	H1ValidationObservation last;
	H1ValidationObservation measured[20];
	uint32_t campaignSequence, measuredStored, active;
};

extern H1ValidationObserverState h1ValidationObserverState;
void h1ValidationObserverInit();
void h1ValidationObserverCampaignBegin(uint32_t sequence);
void h1ValidationObserverPrepare(bool active);
void h1ValidationObserverMark(H1ValidationMark marker);
void h1ValidationObserverAt(H1ValidationMark marker, uint64_t cycles);
void h1ValidationObserverCapture(int32_t index, uint64_t end,
				 uint32_t runSequence, uint32_t clockHz,
				 bool success);
#define H1_VALIDATION_MARK(marker) h1ValidationObserverMark(marker)
#define H1_VALIDATION_AT(marker, cycles) h1ValidationObserverAt(marker, cycles)
#else
#define H1_VALIDATION_MARK(marker) do { } while (false)
#define H1_VALIDATION_AT(marker, cycles) do { } while (false)
#endif
