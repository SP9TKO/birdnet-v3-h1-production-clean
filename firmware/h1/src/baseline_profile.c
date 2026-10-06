// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "baseline_profile.h"
#ifndef ETHOSU85
#define ETHOSU85 1
#endif
#include <ethosu_driver.h>
#include <pmu_ethosu.h>
#include <cmsis_core.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>
#if !defined(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER) || !defined(CONFIG_CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER)
#error "Frozen baseline requires configured 64-bit SysTick cycle extension"
#endif
#if CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC != 400000000 || CONFIG_SYS_CLOCK_TICKS_PER_SEC != 10000
#error "Frozen baseline timer configuration mismatch"
#endif
__attribute__((section(".h1_baseline"), aligned(32)))
struct H1BaselineState h1BaselineState;
_Static_assert(sizeof(struct H1BaselineState) <= 65536, "baseline RAM budget");
static const enum ethosu_pmu_event_type events[H1_BASELINE_EVENTS] = {
 ETHOSU_PMU_NPU_ACTIVE, ETHOSU_PMU_MAC_ACTIVE, ETHOSU_PMU_WD_ACTIVE,
 ETHOSU_PMU_WD_STALLED, ETHOSU_PMU_EXT_RD_TRAN_REQ_STALLED,
 ETHOSU_PMU_SRAM_RD_TRAN_REQ_STALLED, ETHOSU_PMU_SRAM_WR_TRAN_REQ_STALLED
};
void h1BaselineInit(void)
{
 memset(&h1BaselineState, 0, sizeof(h1BaselineState));
 h1BaselineState.cache_selector = SCB->CSSELR;
 if (h1BaselineState.cache_selector == 0) {
  uint32_t c = SCB->CCSIDR;
  h1BaselineState.cache_capacity_bytes =
   (1u << ((c & 7u) + 4u)) * (((c >> 3) & 1023u) + 1u) *
   (((c >> 13) & 32767u) + 1u);
 }
}
bool h1BaselineDiagnosticActive(void)
{
 return h1BaselineState.running && h1BaselineState.mode != H1_BASELINE_ACCEPTANCE;
}
void h1BaselineBeginCampaign(uint32_t mode, uint32_t sequence)
{
 struct H1BaselineTensorMap maps[2];
 memcpy(maps, h1BaselineState.tensors, sizeof(maps));
 h1BaselineInit();
 memcpy(h1BaselineState.tensors, maps, sizeof(maps));
 h1BaselineState.mode = mode;
 h1BaselineState.running = 1;
 h1BaselineState.campaign_sequence = sequence;
 h1BaselineState.requested_samples = mode == H1_BASELINE_ACCEPTANCE ? 100 : 20;
 h1BaselineState.clock_hz = sys_clock_hw_cycles_per_sec();
 h1BaselineState.campaign_start = k_cycle_get_64();
 if (h1BaselineState.clock_hz != 400000000 || !h1BaselineState.cache_capacity_bytes)
  h1BaselineState.error = 1;
}
void h1BaselineBeginRun(int32_t index)
{
 h1BaselineState.current = NULL;
 if (h1BaselineDiagnosticActive()) {
  struct H1BaselineDiagnostic *p = index < 0 ? &h1BaselineState.warmup :
   &h1BaselineState.diagnostic[index];
  memset(p, 0, sizeof(*p));
  h1BaselineState.current = p;
 }
}
void h1BaselineEndRun(int32_t index, uint64_t start, uint64_t end,
 const struct H1RuntimeProfile *profile, uint32_t sequence, uint32_t status,
 const uint32_t crc[7], bool success)
{
 if (!success || end <= start || !profile->valid || profile->clock_hz != 400000000 ||
     profile->backbone.command_count != 1 || profile->backbone.irq_count != 1 ||
     profile->classifier.command_count != 1 || profile->classifier.irq_count != 1 ||
     profile->backbone.lifecycle.copy_count != 1 ||
     profile->classifier.lifecycle.copy_count != 1 ||
     profile->backbone.lifecycle.runtime_init_count != 1 ||
     profile->classifier.lifecycle.runtime_init_count != 1)
  h1BaselineState.error = 2;
 if (!h1BaselineState.first_run_sequence) h1BaselineState.first_run_sequence = sequence;
 h1BaselineState.last_run_sequence = sequence;
 if (index >= 0) {
  h1BaselineState.samples[index] = end - start;
  memcpy(h1BaselineState.boundary_crc[index], crc, 7 * sizeof(uint32_t));
  ++h1BaselineState.measured_completed;
 } else ++h1BaselineState.warmups_completed;
 struct H1BaselineDiagnostic *p = h1BaselineState.current;
 if (p) {
  p->primary_start = start; p->primary_end = end;
  p->run_sequence = sequence; p->status = status;
  memcpy(&p->profile, profile, sizeof(*profile));
  p->stages[0] = p->spectral_cycles;
  p->stages[1] = p->mel_cycles;
  p->stages[2] = p->frontend_end - p->frontend_start - p->spectral_cycles -
   p->mel_cycles + p->quantize_end - p->quantize_start;
  p->stages[3] = profile->backbone.command_start_cycles - p->quantize_end;
  p->stages[4] = profile->backbone.irq_entry_cycles - profile->backbone.command_start_cycles;
  p->stages[5] = p->gem_start - profile->backbone.irq_entry_cycles;
  p->stages[6] = p->gem_end - p->gem_start;
  p->stages[7] = p->bridge_end - p->bridge_start;
  p->stages[8] = profile->classifier.command_start_cycles - p->bridge_end;
  p->stages[9] = profile->classifier.irq_entry_cycles - profile->classifier.command_start_cycles;
  p->stages[10] = end - profile->classifier.irq_entry_cycles;
  p->stages[11] = end - start;
  uint64_t sum = 0;
  for (unsigned i = 0; i < 11; ++i) sum += p->stages[i];
  if (sum > p->stages[11] || p->cache_count > H1_BASELINE_MAX_CACHE)
   h1BaselineState.error = 3;
  else p->residual_cycles = p->stages[11] - sum;
  if (h1BaselineState.mode == H1_BASELINE_PMU)
   for (unsigned i = 0; i < 2; ++i)
    if (!p->pmu[i].configured || p->pmu[i].snapshots != 2 ||
        p->pmu[i].overflow || p->pmu[i].after_cycles <= p->pmu[i].before_cycles)
     h1BaselineState.error = 4;
 }
 h1BaselineState.current = NULL;
}
void h1BaselineEndCampaign(void)
{
 h1BaselineState.campaign_end = k_cycle_get_64();
 h1BaselineState.running = 0;
 h1BaselineState.last_mode = h1BaselineState.mode;
 h1BaselineState.mode = 0;
}
void h1BaselineSpectralGroupBegin(void)
{
 if (h1BaselineDiagnosticActive()) h1BaselineState.spectral_group_start = k_cycle_get_64();
}
void h1BaselineSpectralGroupEnd(void)
{
 if (h1BaselineState.current)
  h1BaselineState.current->spectral_cycles += k_cycle_get_64() - h1BaselineState.spectral_group_start;
}
void h1BaselineMelCycles(uint64_t cycles)
{
 if (h1BaselineState.current) h1BaselineState.current->mel_cycles = cycles;
}
void h1BaselineMark(uint32_t marker, uint64_t cycles)
{
 struct H1BaselineDiagnostic *p = h1BaselineState.current;
 if (!p) return;
 switch (marker) {
 case 0: p->frontend_start = cycles; break; case 1: p->frontend_end = cycles; break;
 case 2: p->quantize_start = cycles; break; case 3: p->quantize_end = cycles; break;
 case 4: p->gem_start = cycles; break; case 5: p->gem_end = cycles; break;
 case 6: p->bridge_start = cycles; break; case 7: p->bridge_end = cycles; break;
 }
}
void h1BaselineTensorBind(uint32_t stage, uint32_t input, uint32_t input_bytes,
 uint32_t output, uint32_t output_bytes, uint32_t arena_used_bytes)
{
 if (h1BaselineState.running && h1BaselineState.mode == H1_BASELINE_ACCEPTANCE) return;
 h1BaselineState.npu_stage = stage;
 struct H1BaselineTensorMap *p = &h1BaselineState.tensors[stage];
 if (p->input && (p->input != input || p->output != output ||
     p->arena_used_bytes != arena_used_bytes)) h1BaselineState.error = 5;
 p->input = input; p->input_bytes = input_bytes;
 p->output = output; p->output_bytes = output_bytes; p->arena_used_bytes = arena_used_bytes;
}
void ethosu_inference_begin(struct ethosu_driver *drv, void *arg)
{
 (void)arg;
 if (h1BaselineState.mode != H1_BASELINE_PMU) return;
 h1BaselineState.pmu_driver = drv;
 struct H1BaselinePmu *p = &h1BaselineState.current->pmu[h1BaselineState.npu_stage];
 if (ETHOSU_PMU_Get_NumEventCounters() != 8) { h1BaselineState.error = 6; return; }
 ETHOSU_PMU_Enable(drv);
 ETHOSU_PMU_CNTR_Disable(drv, UINT32_MAX);
 ETHOSU_PMU_CYCCNT_Reset(drv); ETHOSU_PMU_EVCNTR_ALL_Reset(drv);
 ETHOSU_PMU_Set_CNTR_OVS(drv, UINT32_MAX);
 for (unsigned i = 0; i < H1_BASELINE_EVENTS; ++i) {
  ETHOSU_PMU_Set_EVTYPER(drv, i, events[i]);
  if (ETHOSU_PMU_Get_EVTYPER(drv, i) != events[i]) h1BaselineState.error = 7;
 }
 ETHOSU_PMU_Set_EVTYPER(drv, 7, ETHOSU_PMU_NO_EVENT);
 p->configured = h1BaselineState.error == 0;
}
void h1BaselineCommandSnapshot(void)
{
 if (h1BaselineState.mode != H1_BASELINE_PMU || !h1BaselineState.pmu_driver) return;
 struct ethosu_driver *drv = h1BaselineState.pmu_driver;
 struct H1BaselinePmu *p = &h1BaselineState.current->pmu[h1BaselineState.npu_stage];
 p->before_cycles = ETHOSU_PMU_Get_CCNTR(drv);
 for (unsigned i = 0; i < H1_BASELINE_EVENTS; ++i) p->before[i] = ETHOSU_PMU_Get_EVCNTR(drv, i);
 ETHOSU_PMU_CNTR_Enable(drv, ETHOSU_PMU_CCNT_Msk | 0x7fu);
 ++p->snapshots;
}
void h1BaselineIrqSnapshot(void)
{
 if (h1BaselineState.mode != H1_BASELINE_PMU || !h1BaselineState.pmu_driver) return;
 struct ethosu_driver *drv = h1BaselineState.pmu_driver;
 struct H1BaselinePmu *p = &h1BaselineState.current->pmu[h1BaselineState.npu_stage];
 ETHOSU_PMU_CNTR_Disable(drv, UINT32_MAX);
 p->after_cycles = ETHOSU_PMU_Get_CCNTR(drv);
 for (unsigned i = 0; i < H1_BASELINE_EVENTS; ++i) p->after[i] = ETHOSU_PMU_Get_EVCNTR(drv, i);
 p->overflow = ETHOSU_PMU_Get_CNTR_OVS(drv); ++p->snapshots;
}
void ethosu_inference_end(struct ethosu_driver *drv, void *arg)
{
 (void)arg;
 if (h1BaselineState.mode == H1_BASELINE_PMU && h1BaselineState.pmu_driver == drv) {
  ETHOSU_PMU_Disable(drv); h1BaselineState.pmu_driver = NULL;
 }
}
bool ethosu_area_needs_flush_dcache(const void *p, size_t bytes);
bool ethosu_area_needs_invalidate_dcache(const void *p, size_t bytes);
void __real_ethosu_flush_dcache(uint32_t *p, size_t bytes);
void __real_ethosu_invalidate_dcache(uint32_t *p, size_t bytes);
static struct H1BaselineCache *cache_begin(uint32_t address, uint32_t bytes,
 uint32_t flags, bool eligible)
{
 struct H1BaselineDiagnostic *p = h1BaselineState.current;
 if (!p) return NULL;
 if (p->cache_count >= H1_BASELINE_MAX_CACHE) { h1BaselineState.error = 8; return NULL; }
 struct H1BaselineCache *c = &p->cache[p->cache_count++];
 c->stage = h1BaselineState.npu_stage; c->address = address; c->requested_bytes = bytes;
 c->rounded_address = address & ~31u;
 c->rounded_bytes = ((address + bytes + 31u) & ~31u) - c->rounded_address;
 if (!eligible) { c->flags = H1_BASELINE_CACHE_BARRIER; c->maintained_bytes = 0; }
 else if (bytes > 131072u) {
  c->flags = flags | H1_BASELINE_CACHE_WHOLE;
  if (flags & H1_BASELINE_CACHE_INVALIDATE) c->flags |= H1_BASELINE_CACHE_CLEAN;
  c->maintained_bytes = h1BaselineState.cache_capacity_bytes;
 } else { c->flags = flags | H1_BASELINE_CACHE_RANGE; c->maintained_bytes = c->rounded_bytes; }
 c->start_cycles = k_cycle_get_64(); return c;
}
void __wrap_ethosu_flush_dcache(uint32_t *p, size_t bytes)
{
 struct H1BaselineCache *c = NULL;
 if (h1BaselineDiagnosticActive()) c = cache_begin((uint32_t)(uintptr_t)p, bytes,
  H1_BASELINE_CACHE_CLEAN, p && ethosu_area_needs_flush_dcache(p, bytes));
 __real_ethosu_flush_dcache(p, bytes);
 if (c) c->end_cycles = k_cycle_get_64();
}
void __wrap_ethosu_invalidate_dcache(uint32_t *p, size_t bytes)
{
 struct H1BaselineCache *c = NULL;
 if (h1BaselineDiagnosticActive()) c = cache_begin((uint32_t)(uintptr_t)p, bytes,
  H1_BASELINE_CACHE_INVALIDATE, p && ethosu_area_needs_invalidate_dcache(p, bytes));
 __real_ethosu_invalidate_dcache(p, bytes);
 if (c) c->end_cycles = k_cycle_get_64();
}
void h1BaselineGuardInvalidate(uint32_t address, uint32_t bytes)
{
 if (h1BaselineDiagnosticActive()) {
  struct H1BaselineCache *c = cache_begin(address, bytes, H1_BASELINE_CACHE_INVALIDATE, true);
  if (c) c->end_cycles = c->start_cycles;
 }
}

void h1BaselineGuardInvalidateDone(void)
{
 struct H1BaselineDiagnostic *p = h1BaselineState.current;
 if (p && p->cache_count) p->cache[p->cache_count - 1].end_cycles = k_cycle_get_64();
}

uint32_t h1BaselinePmuCounters(void)
{
 return ETHOSU_PMU_Get_NumEventCounters();
}
