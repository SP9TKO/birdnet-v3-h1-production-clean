/*
 * Development-only H1 compute timing. The command-start and IRQ-entry hooks
 * preserve the previously qualified A/B/C/D Ethos-U timing boundaries.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define H1_PROFILE_VERSION UINT32_C(2)

struct H1ModelLifecycleProfile {
	uint32_t model_source_address;
	uint32_t model_destination_address;
	uint32_t model_bytes;
	uint32_t model_source_crc32;
	uint32_t model_destination_crc32;
	int32_t model_memcmp_result;
	uint32_t arena_used_bytes;
	uint32_t input_bytes;
	uint32_t output_bytes;

	uint32_t source_crc_count;
	uint32_t copy_count;
	uint32_t destination_crc_count;
	uint32_t memcmp_count;
	uint32_t validate_count;
	uint32_t runtime_init_count;
	uint32_t allocate_tensors_count;
	uint32_t tensor_bind_count;
	uint32_t input_copy_count;
	uint32_t output_copy_count;

	volatile uint32_t cache_prepare_count;
	volatile uint32_t cache_prepare_address;
	volatile uint32_t cache_prepare_bytes;

	uint64_t lifecycle_start_cycles;
	uint64_t lifecycle_end_cycles;
	uint64_t lifecycle_cycles;
	uint64_t source_crc_cycles;
	uint64_t copy_cycles;
	uint64_t destination_crc_cycles;
	uint64_t crc_cycles;
	uint64_t memcmp_cycles;
	uint64_t validate_cycles;
	uint64_t runtime_init_cycles;
	uint64_t allocate_tensors_cycles;
	uint64_t tensor_bind_cycles;
	uint64_t input_copy_cycles;
	uint64_t output_copy_cycles;
	volatile uint64_t cache_prepare_start_cycles;
	volatile uint64_t cache_prepare_end_cycles;
	uint64_t cache_prepare_cycles;

	uint32_t lifecycle_us;
	uint32_t source_crc_us;
	uint32_t copy_us;
	uint32_t destination_crc_us;
	uint32_t crc_us;
	uint32_t memcmp_us;
	uint32_t validate_us;
	uint32_t runtime_init_us;
	uint32_t allocate_tensors_us;
	uint32_t tensor_bind_us;
	uint32_t input_copy_us;
	uint32_t output_copy_us;
	uint32_t cache_prepare_us;
};

struct H1NpuProfile {
	struct H1ModelLifecycleProfile lifecycle;
	volatile uint64_t invoke_start_cycles; /* A */
	volatile uint64_t command_start_cycles; /* B */
	volatile uint64_t irq_entry_cycles; /* C */
	volatile uint64_t invoke_end_cycles; /* D */
	volatile uint32_t command_count;
	volatile uint32_t irq_count;
	int32_t invoke_status;
	uint32_t valid;
	uint64_t invoke_cycles;
	uint64_t submit_to_irq_cycles;
	uint32_t invoke_us;
	uint32_t submit_to_irq_us;
};

struct H1RuntimeProfile {
	uint32_t profile_version;
	uint32_t valid;
	uint32_t clock_hz;
	uint32_t transport_excluded;
	uint32_t unassociated_command_count;
	uint32_t unassociated_irq_count;
	uint32_t unassociated_cache_prepare_count;

	uint64_t pre_frontend_overhead_cycles;
	uint32_t pre_frontend_overhead_us;
	uint64_t frontend_cycles;
	uint32_t frontend_us;
	uint64_t frontend_to_backbone_quantize_cycles;
	uint32_t frontend_to_backbone_quantize_us;

	uint64_t frontend_to_backbone_invoke_cycles;
	uint32_t frontend_to_backbone_invoke_us;
	struct H1NpuProfile backbone;
	uint64_t backbone_to_gem_handoff_cycles;
	uint32_t backbone_to_gem_handoff_us;

	uint64_t gem_cycles;
	uint32_t gem_us;
	uint64_t embedding_to_classifier_quantize_cycles;
	uint32_t embedding_to_classifier_quantize_us;
	uint64_t gem_to_classifier_handoff_cycles;
	uint32_t gem_to_classifier_handoff_us;

	struct H1NpuProfile classifier;
	uint64_t classifier_to_postprocess_handoff_cycles;
	uint32_t classifier_to_postprocess_handoff_us;

	uint64_t postprocess_cycles;
	uint32_t postprocess_us;
	uint64_t total_compute_cycles;
	uint32_t total_compute_us;
};

void h1ProfileReset(struct H1RuntimeProfile *profile);
uint64_t h1ProfileNow(void);
uint32_t h1ProfileCyclesToUs(uint64_t cycles, uint32_t clock_hz);
void h1ProfileInvokeBegin(struct H1NpuProfile *profile);
void h1ProfileInvokeEnd(struct H1NpuProfile *profile, int32_t invoke_status);
void h1ProfileCommandStart(void);
void h1ProfileIrqEntry(void);
bool h1ProfileFinalize(struct H1RuntimeProfile *profile);

void h1ProfileCachePrepareBegin(uint32_t address, uint32_t bytes);
void h1ProfileCachePrepareEnd(void);
#ifdef __cplusplus
}
#endif
