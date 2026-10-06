// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "runtime_profile.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define H1_BASELINE_ACCEPTANCE 1u
#define H1_BASELINE_DIAGNOSTIC 2u
#define H1_BASELINE_PMU 3u
#define H1_BASELINE_MAX_SAMPLES 100u
#define H1_BASELINE_DIAGNOSTIC_SAMPLES 20u
#define H1_BASELINE_MAX_CACHE 8u
#define H1_BASELINE_EVENTS 7u
#define H1_BASELINE_CACHE_CLEAN 1u
#define H1_BASELINE_CACHE_INVALIDATE 2u
#define H1_BASELINE_CACHE_WHOLE 4u
#define H1_BASELINE_CACHE_RANGE 8u
#define H1_BASELINE_CACHE_BARRIER 16u
struct H1BaselinePmu {
 uint64_t before_cycles, after_cycles;
 uint32_t before[H1_BASELINE_EVENTS], after[H1_BASELINE_EVENTS];
 uint32_t overflow, configured, snapshots;
};
struct H1BaselineCache {
 uint64_t start_cycles, end_cycles;
 uint32_t stage, address, requested_bytes, rounded_address, rounded_bytes;
 uint32_t maintained_bytes, flags;
};
struct H1BaselineDiagnostic {
 uint64_t primary_start, primary_end, stages[12], residual_cycles;
 uint64_t spectral_cycles, mel_cycles;
 uint64_t frontend_start, frontend_end, quantize_start, quantize_end;
 uint64_t gem_start, gem_end, bridge_start, bridge_end;
 struct H1RuntimeProfile profile;
 struct H1BaselinePmu pmu[2];
 struct H1BaselineCache cache[H1_BASELINE_MAX_CACHE];
 uint32_t cache_count, run_sequence, status;
};
struct H1BaselineTensorMap {
 uint32_t input, input_bytes, output, output_bytes, arena_used_bytes;
};
struct H1BaselineState {
 volatile uint32_t mode, running, error;
 uint32_t warmups_completed, measured_completed, requested_samples, clock_hz, last_mode;
 uint32_t campaign_sequence, first_run_sequence, last_run_sequence;
 uint32_t irq_before, irq_after, cache_capacity_bytes, cache_selector;
 uint64_t campaign_start, campaign_end;
 uint64_t samples[H1_BASELINE_MAX_SAMPLES];
 uint32_t boundary_crc[H1_BASELINE_MAX_SAMPLES][7];
 struct H1BaselineDiagnostic diagnostic[H1_BASELINE_DIAGNOSTIC_SAMPLES];
 struct H1BaselineDiagnostic warmup;
 struct H1BaselineTensorMap tensors[2];
 struct H1BaselineDiagnostic *current;
 void *pmu_driver;
 volatile uint32_t npu_stage;
 uint64_t spectral_group_start;
};
extern struct H1BaselineState h1BaselineState;
void h1BaselineInit(void);
uint32_t h1BaselinePmuCounters(void);
void h1BaselineBeginCampaign(uint32_t mode, uint32_t sequence);
void h1BaselineBeginRun(int32_t measured_index);
void h1BaselineEndRun(int32_t measured_index, uint64_t start, uint64_t end,
 const struct H1RuntimeProfile *profile, uint32_t sequence, uint32_t status,
 const uint32_t boundary_crc[7], bool success);
void h1BaselineEndCampaign(void);
bool h1BaselineDiagnosticActive(void);
void h1BaselineSpectralGroupBegin(void);
void h1BaselineSpectralGroupEnd(void);
void h1BaselineMelCycles(uint64_t cycles);
void h1BaselineMark(uint32_t marker, uint64_t cycles);
void h1BaselineTensorBind(uint32_t stage, uint32_t input, uint32_t input_bytes,
 uint32_t output, uint32_t output_bytes, uint32_t arena_used_bytes);
void h1BaselineCommandSnapshot(void);
void h1BaselineIrqSnapshot(void);
void h1BaselineGuardInvalidate(uint32_t address, uint32_t bytes);
void h1BaselineGuardInvalidateDone(void);
#ifdef __cplusplus
}
#endif
