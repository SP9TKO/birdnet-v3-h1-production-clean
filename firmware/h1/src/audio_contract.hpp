#pragma once

#include <cstddef>
#include <cstdint>

constexpr uint32_t H1_MIC_SAMPLE_RATE_HZ = 32000;
constexpr uint32_t H1_MIC_WINDOW_SAMPLES = 96000;
constexpr uint32_t H1_MIC_STRIDE_SAMPLES = 32000;
constexpr uint32_t H1_MIC_OVERLAP_SAMPLES = 64000;
constexpr uint32_t H1_MIC_RING_SAMPLES = 160000;
constexpr uint32_t H1_MIC_BLOCK_BYTES = 2048;
constexpr uint32_t H1_MIC_BLOCK_SAMPLES = H1_MIC_BLOCK_BYTES / sizeof(int16_t);
constexpr uint32_t H1_MIC_SLAB_BLOCKS = 8;
constexpr uint32_t H1_MIC_WINDOW_BYTES = H1_MIC_WINDOW_SAMPLES * sizeof(int16_t);
constexpr uint32_t H1_MIC_PCM_CHUNK_MAX_BYTES = 1024;
constexpr uint64_t H1_MIC_LATEST_WINDOW = UINT64_MAX;

static_assert(H1_MIC_WINDOW_SAMPLES == 3 * H1_MIC_SAMPLE_RATE_HZ);
static_assert(H1_MIC_STRIDE_SAMPLES == H1_MIC_SAMPLE_RATE_HZ);
static_assert(H1_MIC_WINDOW_SAMPLES - H1_MIC_STRIDE_SAMPLES ==
	      H1_MIC_OVERLAP_SAMPLES);
static_assert(H1_MIC_RING_SAMPLES >=
	      H1_MIC_WINDOW_SAMPLES + H1_MIC_STRIDE_SAMPLES);
static_assert(H1_MIC_BLOCK_BYTES % sizeof(int16_t) == 0);

enum class H1MicMode : uint32_t {
	Off = 0,
	CaptureOnly = 1,
	WindowValidate = 2,
	H1Single = 3,
};

enum class H1MicState : uint32_t {
	Off = 0,
	Starting = 1,
	Capture = 2,
	WindowReady = 3,
	Error = 4,
};

enum class H1MicInferenceState : uint32_t {
	Idle = 0,
	Running = 1,
	ResultReady = 2,
};

enum class H1MicError : int32_t {
	None = 0,
	NotInitialized = 1,
	AlreadyRunning = 2,
	InvalidMode = 3,
	TriggerStart = 4,
	TriggerStop = 5,
	ReadTimeout = 6,
	ReadFailure = 7,
	InvalidBlockSize = 8,
	Fifo = 9,
	SlabExhausted = 10,
	DriverQueueOverrun = 11,
	Sequence = 12,
	RingFrontier = 13,
	NoCompleteWindow = 14,
	WindowNotRetained = 15,
	ModeDisallowsOperation = 16,
	NoSelectedWindow = 17,
};

struct H1PcmStatistics {
	uint64_t sampleCount;
	int16_t minimum;
	int16_t maximum;
	int64_t sum;
	uint64_t sumSquares;
	uint64_t positiveClippingCount;
	uint64_t negativeClippingCount;
	uint64_t zeroCount;
	uint32_t maximumAdjacentJump;
};

struct H1SelectedWindowInfo {
	bool valid;
	uint64_t sequence;
	uint64_t startSample;
	uint64_t endSample;
	uint32_t physicalStartIndex;
	bool wraps;
	uint32_t crc32;
	char sha256[65];
	H1PcmStatistics statistics;
};

struct H1MicStatusSnapshot {
	bool initialized;
	bool running;
	H1MicMode mode;
	H1MicState state;
	H1MicInferenceState inferenceState;
	H1MicError lastError;
	int32_t initStatus;
	int32_t lastDriverStatus;
	uint64_t driverPublishedBlocks;
	uint64_t consumedBlocks;
	uint64_t expectedSequence;
	uint64_t sequenceFaults;
	uint64_t fifoErrors;
	uint64_t slabMisses;
	uint64_t driverQueueOverruns;
	uint64_t readTimeouts;
	uint64_t invalidBlockSizes;
	uint32_t queueOccupancy;
	uint32_t queueHighWater;
	uint32_t slabUsed;
	uint32_t slabHighWater;
	uint64_t firstPublishCycles;
	uint64_t lastPublishCycles;
	uint32_t timingClockHz;
	uint64_t totalSamplesCommitted;
	uint64_t oldestRetainedSample;
	uint32_t ringWriteIndex;
	uint64_t ringFrontierFaults;
	uint64_t completeWindows;
	uint64_t latestWindowSequence;
	uint64_t latestWindowStartSample;
	uint64_t latestWindowEndSample;
	uint64_t windowGeometryFaults;
	H1PcmStatistics captureStatistics;
	H1SelectedWindowInfo selectedWindow;
};

const char *h1MicModeName(H1MicMode mode);
const char *h1MicStateName(H1MicState state);
const char *h1MicInferenceStateName(H1MicInferenceState state);
const char *h1MicErrorName(H1MicError error);
