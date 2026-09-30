#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct H1PdmDriverTelemetry {
	uint64_t published_blocks;
	uint64_t fifo_errors;
	uint64_t slab_misses;
	uint64_t queue_overruns;
	uint64_t first_publish_cycles;
	uint64_t last_publish_cycles;
	uint32_t queue_occupancy;
	uint32_t queue_high_water;
};

void h1PdmDriverTelemetryReset(void);
void h1PdmDriverBlockPublished(uint32_t queue_occupancy);
void h1PdmDriverFifoError(void);
void h1PdmDriverSlabMiss(void);
void h1PdmDriverQueueOverrun(void);
void h1PdmDriverTelemetryGet(struct H1PdmDriverTelemetry *telemetry);

#ifdef __cplusplus
}
#endif
