#include "runtime_profile.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

static volatile struct H1NpuProfile *active_npu_profile;
static volatile uint32_t unassociated_command_count;
static volatile uint32_t unassociated_irq_count;
static volatile uint32_t unassociated_cache_prepare_count;

static bool finalize_lifecycle(struct H1ModelLifecycleProfile *profile,
			       uint32_t clock_hz)
{
	if (profile->model_source_address == 0 ||
	    profile->model_destination_address == 0 || profile->model_bytes == 0 ||
	    profile->model_source_crc32 != profile->model_destination_crc32 ||
	    profile->model_memcmp_result != 0 || profile->arena_used_bytes == 0 ||
	    profile->input_bytes == 0 || profile->output_bytes == 0 ||
	    profile->source_crc_count != 1 || profile->copy_count != 1 ||
	    profile->destination_crc_count != 1 || profile->memcmp_count != 1 ||
	    profile->validate_count != 1 || profile->runtime_init_count != 1 ||
	    profile->allocate_tensors_count != 1 || profile->tensor_bind_count != 1 ||
	    profile->input_copy_count != 1 || profile->output_copy_count != 1 ||
	    profile->cache_prepare_count != 1 ||
	    profile->cache_prepare_address == 0 || profile->cache_prepare_bytes == 0 ||
	    profile->lifecycle_end_cycles <= profile->lifecycle_start_cycles ||
	    profile->cache_prepare_end_cycles <=
		    profile->cache_prepare_start_cycles ||
	    profile->source_crc_cycles == 0 || profile->copy_cycles == 0 ||
	    profile->destination_crc_cycles == 0 || profile->memcmp_cycles == 0 ||
	    profile->validate_cycles == 0 || profile->runtime_init_cycles == 0 ||
	    profile->allocate_tensors_cycles == 0 ||
	    profile->tensor_bind_cycles == 0 || profile->input_copy_cycles == 0 ||
	    profile->output_copy_cycles == 0) {
		return false;
	}

	profile->lifecycle_cycles =
		profile->lifecycle_end_cycles - profile->lifecycle_start_cycles;
	profile->crc_cycles =
		profile->source_crc_cycles + profile->destination_crc_cycles;
	profile->cache_prepare_cycles =
		profile->cache_prepare_end_cycles -
		profile->cache_prepare_start_cycles;

#define H1_CONVERT_LIFECYCLE(field) \
	profile->field##_us = h1ProfileCyclesToUs(profile->field##_cycles, clock_hz)
	H1_CONVERT_LIFECYCLE(lifecycle);
	H1_CONVERT_LIFECYCLE(source_crc);
	H1_CONVERT_LIFECYCLE(copy);
	H1_CONVERT_LIFECYCLE(destination_crc);
	H1_CONVERT_LIFECYCLE(crc);
	H1_CONVERT_LIFECYCLE(memcmp);
	H1_CONVERT_LIFECYCLE(validate);
	H1_CONVERT_LIFECYCLE(runtime_init);
	H1_CONVERT_LIFECYCLE(allocate_tensors);
	H1_CONVERT_LIFECYCLE(tensor_bind);
	H1_CONVERT_LIFECYCLE(input_copy);
	H1_CONVERT_LIFECYCLE(output_copy);
	H1_CONVERT_LIFECYCLE(cache_prepare);
#undef H1_CONVERT_LIFECYCLE
	return true;
}
static bool finalize_npu(struct H1NpuProfile *profile, uint32_t clock_hz)
{
	const uint64_t a = profile->invoke_start_cycles;
	const uint64_t b = profile->command_start_cycles;
	const uint64_t c = profile->irq_entry_cycles;
	const uint64_t d = profile->invoke_end_cycles;

	if (!finalize_lifecycle(&profile->lifecycle, clock_hz) ||
	    profile->invoke_status != 0 || profile->command_count != 1 ||
	    profile->irq_count != 1 || !(a < b && b < c && c < d)) {
		profile->valid = 0;
		return false;
	}
	profile->invoke_cycles = d - a;
	profile->submit_to_irq_cycles = c - b;
	profile->invoke_us = h1ProfileCyclesToUs(profile->invoke_cycles, clock_hz);
	profile->submit_to_irq_us =
		h1ProfileCyclesToUs(profile->submit_to_irq_cycles, clock_hz);
	profile->valid = 1;
	return true;
}

void h1ProfileReset(struct H1RuntimeProfile *profile)
{
	memset(profile, 0, sizeof(*profile));
	profile->profile_version = H1_PROFILE_VERSION;
	profile->clock_hz = sys_clock_hw_cycles_per_sec();
	profile->transport_excluded = 1;
	active_npu_profile = NULL;
	unassociated_command_count = 0;
	unassociated_irq_count = 0;
	unassociated_cache_prepare_count = 0;
}

uint64_t h1ProfileNow(void)
{
	return k_cycle_get_64();
}

uint32_t h1ProfileCyclesToUs(uint64_t cycles, uint32_t clock_hz)
{
	if (clock_hz == 0) {
		return 0;
	}
	return (uint32_t)((cycles * UINT64_C(1000000) + clock_hz / 2u) / clock_hz);
}

void h1ProfileInvokeBegin(struct H1NpuProfile *profile)
{
	active_npu_profile = profile;
	profile->invoke_start_cycles = k_cycle_get_64();
}

void h1ProfileInvokeEnd(struct H1NpuProfile *profile, int32_t invoke_status)
{
	profile->invoke_end_cycles = k_cycle_get_64();
	active_npu_profile = NULL;
	profile->invoke_status = invoke_status;
}

void h1ProfileCommandStart(void)
{
	const uint64_t now = k_cycle_get_64();
	volatile struct H1NpuProfile *profile = active_npu_profile;
	if (profile != NULL) {
		if (profile->command_count == 0) {
			profile->command_start_cycles = now;
		}
		++profile->command_count;
	} else {
		++unassociated_command_count;
	}
}

void h1ProfileIrqEntry(void)
{
	const uint64_t now = k_cycle_get_64();
	volatile struct H1NpuProfile *profile = active_npu_profile;
	if (profile != NULL) {
		if (profile->irq_count == 0) {
			profile->irq_entry_cycles = now;
		}
		++profile->irq_count;
	} else {
		++unassociated_irq_count;
	}
}


void h1ProfileCachePrepareBegin(uint32_t address, uint32_t bytes)
{
	const uint64_t now = k_cycle_get_64();
	volatile struct H1NpuProfile *profile = active_npu_profile;
	if (profile != NULL) {
		volatile struct H1ModelLifecycleProfile *lifecycle =
			&profile->lifecycle;
		if (lifecycle->cache_prepare_count == 0) {
			lifecycle->cache_prepare_start_cycles = now;
			lifecycle->cache_prepare_address = address;
			lifecycle->cache_prepare_bytes = bytes;
		}
		++lifecycle->cache_prepare_count;
	} else {
		++unassociated_cache_prepare_count;
	}
}

void h1ProfileCachePrepareEnd(void)
{
	const uint64_t now = k_cycle_get_64();
	volatile struct H1NpuProfile *profile = active_npu_profile;
	if (profile != NULL && profile->lifecycle.cache_prepare_count != 0) {
		profile->lifecycle.cache_prepare_end_cycles = now;
	} else {
		++unassociated_cache_prepare_count;
	}
}

bool h1ProfileFinalize(struct H1RuntimeProfile *profile)
{
	const uint32_t clock_hz = profile->clock_hz;
	profile->unassociated_command_count = unassociated_command_count;
	profile->unassociated_irq_count = unassociated_irq_count;
	profile->unassociated_cache_prepare_count =
		unassociated_cache_prepare_count;
	if (clock_hz == 0 || profile->transport_excluded != 1 ||
	    profile->unassociated_command_count != 0 ||
	    profile->unassociated_irq_count != 0 ||
	    profile->unassociated_cache_prepare_count != 0 ||
	    !finalize_npu(&profile->backbone, clock_hz) ||
	    !finalize_npu(&profile->classifier, clock_hz)) {
		profile->valid = 0;
		return false;
	}

#define H1_CONVERT(field) profile->field##_us = h1ProfileCyclesToUs(profile->field##_cycles, clock_hz)
	H1_CONVERT(pre_frontend_overhead);
	H1_CONVERT(frontend);
	H1_CONVERT(frontend_to_backbone_quantize);
	H1_CONVERT(frontend_to_backbone_invoke);
	H1_CONVERT(backbone_to_gem_handoff);
	H1_CONVERT(gem);
	H1_CONVERT(embedding_to_classifier_quantize);
	H1_CONVERT(gem_to_classifier_handoff);
	H1_CONVERT(classifier_to_postprocess_handoff);
	H1_CONVERT(postprocess);
	H1_CONVERT(total_compute);
#undef H1_CONVERT

	profile->valid = profile->frontend_cycles > 0 &&
		profile->frontend_to_backbone_quantize_cycles > 0 &&
		profile->frontend_to_backbone_invoke_cycles > 0 &&
		profile->backbone_to_gem_handoff_cycles > 0 && profile->gem_cycles > 0 &&
		profile->embedding_to_classifier_quantize_cycles > 0 &&
		profile->gem_to_classifier_handoff_cycles > 0 &&
		profile->classifier_to_postprocess_handoff_cycles > 0 &&
		profile->postprocess_cycles > 0 && profile->total_compute_cycles > 0;
	return profile->valid != 0;
}
