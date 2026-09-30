#include "audio_pdm.hpp"

#include "audio_pdm_driver_hooks.h"
#include "model_storage.hpp"
#include "pcm_ring.hpp"
#include "run_state.hpp"
#include "sha256.hpp"
#include "sliding_window.hpp"

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pdm/pdm_alif.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/time_units.h>

namespace {
constexpr uint8_t kChannel = 4;
constexpr uint32_t kChannelMap = PDM_MASK_CHANNEL_4;
constexpr uint32_t kPhase = 0x0000001f;
constexpr uint32_t kGain = 0x0000000d;
constexpr uint32_t kPeakDetectThreshold = 0x00060002;
constexpr uint32_t kPeakDetectInterval = 0x0004002d;
constexpr uint32_t kIirCoefficient = 0x00000004;
constexpr int32_t kReadTimeoutMs = 500;
constexpr size_t kThreadStackBytes = 4096;

constexpr uint32_t kFirCoefficients[PDM_MAX_FIR_COEFFICIENT] = {
	0x00000001, 0x00000003, 0x00000003, 0x000007f4, 0x00000004,
	0x000007ed, 0x000007f5, 0x000007f4, 0x000007d3, 0x000007fe,
	0x000007bc, 0x000007e5, 0x000007d9, 0x00000793, 0x00000029,
	0x0000072c, 0x00000072, 0x000002fd,
};

#define H1_PDM_NODE DT_ALIAS(pdm_audio)
BUILD_ASSERT(DT_NODE_HAS_STATUS(H1_PDM_NODE, okay),
	     "The PDM microphone devicetree alias must be enabled");

const device *const kPdm = DEVICE_DT_GET(H1_PDM_NODE);

k_mem_slab gPdmSlab;
__attribute__((section("SRAM1.audio"), aligned(32)))
uint8_t gPdmSlabBuffer[H1_MIC_BLOCK_BYTES * H1_MIC_SLAB_BLOCKS];
K_THREAD_STACK_DEFINE(gCaptureStack, kThreadStackBytes);
K_SEM_DEFINE(gCaptureStart, 0, 1);
K_SEM_DEFINE(gCaptureStopped, 0, 1);
K_MUTEX_DEFINE(gAudioMutex);

k_thread gCaptureThread;
atomic_t gRunRequested;
atomic_t gThreadActive;
bool gThreadCreated;
bool gInitialized;
int32_t gInitStatus = -EAGAIN;
int32_t gLastDriverStatus;
H1MicMode gMode = H1MicMode::Off;
H1MicState gState = H1MicState::Off;
H1MicInferenceState gInferenceState = H1MicInferenceState::Idle;
H1MicError gLastError = H1MicError::None;
uint64_t gConsumedBlocks;
uint64_t gExpectedSequence;
uint64_t gSequenceFaults;
uint64_t gReadTimeouts;
uint64_t gInvalidBlockSizes;
uint32_t gSlabHighWater;
H1PcmStatistics gCaptureStatistics;
bool gHavePreviousSample;
int16_t gPreviousSample;
H1SelectedWindowInfo gSelectedWindow;

k_spinlock gDriverLock;
H1PdmDriverTelemetry gDriverTelemetry;

H1PcmRing &ring()
{
	static H1PcmRing instance(h1PcmRingStorage(), H1_MIC_RING_SAMPLES);
	return instance;
}

H1SlidingWindowTracker &windows()
{
	static H1SlidingWindowTracker instance(H1_MIC_WINDOW_SAMPLES,
					       H1_MIC_STRIDE_SAMPLES);
	return instance;
}

uint32_t crc32(const uint8_t *data, size_t bytes)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t index = 0; index < bytes; ++index) {
		crc ^= data[index];
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^
			      (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

void resetStatistics(H1PcmStatistics &statistics)
{
	memset(&statistics, 0, sizeof(statistics));
	statistics.minimum = INT16_MAX;
	statistics.maximum = INT16_MIN;
}

H1PcmStatistics reportableStatistics(H1PcmStatistics statistics)
{
	if (statistics.sampleCount == 0) {
		statistics.minimum = 0;
		statistics.maximum = 0;
	}
	return statistics;
}

void updateStatistics(H1PcmStatistics &statistics, const int16_t *samples,
		      uint32_t count, bool &havePrevious, int16_t &previous)
{
	for (uint32_t index = 0; index < count; ++index) {
		const int16_t sample = samples[index];
		if (sample < statistics.minimum) {
			statistics.minimum = sample;
		}
		if (sample > statistics.maximum) {
			statistics.maximum = sample;
		}
		statistics.sum += sample;
		const int64_t extended = sample;
		statistics.sumSquares += uint64_t(extended * extended);
		statistics.positiveClippingCount += sample == INT16_MAX;
		statistics.negativeClippingCount += sample == INT16_MIN;
		statistics.zeroCount += sample == 0;
		if (havePrevious) {
			int32_t jump = int32_t(sample) - int32_t(previous);
			if (jump < 0) {
				jump = -jump;
			}
			if (uint32_t(jump) > statistics.maximumAdjacentJump) {
				statistics.maximumAdjacentJump = uint32_t(jump);
			}
		}
		previous = sample;
		havePrevious = true;
		++statistics.sampleCount;
	}
}

H1PcmStatistics calculateStatistics(const int16_t *samples, uint32_t count)
{
	H1PcmStatistics statistics;
	resetStatistics(statistics);
	bool havePrevious = false;
	int16_t previous = 0;
	updateStatistics(statistics, samples, count, havePrevious, previous);
	return reportableStatistics(statistics);
}

void setStateError(H1MicError error, int32_t driverStatus)
{
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	gLastError = error;
	gLastDriverStatus = driverStatus;
	gState = H1MicState::Error;
	k_mutex_unlock(&gAudioMutex);
}

bool driverFault(H1MicError &error)
{
	H1PdmDriverTelemetry telemetry{};
	h1PdmDriverTelemetryGet(&telemetry);
	if (telemetry.fifo_errors != 0) {
		error = H1MicError::Fifo;
		return true;
	}
	if (telemetry.slab_misses != 0) {
		error = H1MicError::SlabExhausted;
		return true;
	}
	if (telemetry.queue_overruns != 0) {
		error = H1MicError::DriverQueueOverrun;
		return true;
	}
	return false;
}

void finishCaptureThread()
{
	atomic_clear(&gThreadActive);
	k_sem_give(&gCaptureStopped);
}

void failCapture(H1MicError error, int32_t driverStatus)
{
	atomic_clear(&gRunRequested);
	setStateError(error, driverStatus);
	(void)dmic_trigger(kPdm, DMIC_TRIGGER_STOP);
	finishCaptureThread();
}

void captureThread(void *, void *, void *)
{
	for (;;) {
		k_sem_take(&gCaptureStart, K_FOREVER);
		while (atomic_get(&gRunRequested) != 0) {
			void *buffer = nullptr;
			size_t bytes = 0;
			const int status = dmic_read(kPdm, 0, &buffer, &bytes,
						     kReadTimeoutMs);
			if (status != 0) {
				if (atomic_get(&gRunRequested) == 0) {
					break;
				}
				H1MicError error = H1MicError::ReadFailure;
				if (status == -EAGAIN || status == -ETIMEDOUT) {
					k_mutex_lock(&gAudioMutex, K_FOREVER);
					++gReadTimeouts;
					k_mutex_unlock(&gAudioMutex);
					error = H1MicError::ReadTimeout;
				}
				H1MicError hookError = H1MicError::None;
				if (driverFault(hookError)) {
					error = hookError;
				}
				failCapture(error, status);
				goto next_start;
			}
			if (atomic_get(&gRunRequested) == 0) {
				k_mem_slab_free(&gPdmSlab, buffer);
				break;
			}
			if (bytes != H1_MIC_BLOCK_BYTES || buffer == nullptr) {
				k_mutex_lock(&gAudioMutex, K_FOREVER);
				++gInvalidBlockSizes;
				k_mutex_unlock(&gAudioMutex);
				if (buffer != nullptr) {
					k_mem_slab_free(&gPdmSlab, buffer);
				}
				failCapture(H1MicError::InvalidBlockSize, -EMSGSIZE);
				goto next_start;
			}

			H1MicError hookError = H1MicError::None;
			if (driverFault(hookError)) {
				k_mem_slab_free(&gPdmSlab, buffer);
				failCapture(hookError, -EIO);
				goto next_start;
			}

			H1PdmDriverTelemetry driver{};
			h1PdmDriverTelemetryGet(&driver);
			k_mutex_lock(&gAudioMutex, K_FOREVER);
			const uint64_t sequence = gConsumedBlocks;
			if (driver.published_blocks <= sequence ||
			    sequence != gExpectedSequence) {
				++gSequenceFaults;
				k_mutex_unlock(&gAudioMutex);
				k_mem_slab_free(&gPdmSlab, buffer);
				failCapture(H1MicError::Sequence, -EILSEQ);
				goto next_start;
			}
			const auto *samples = static_cast<const int16_t *>(buffer);
			if (!ring().commit(samples, H1_MIC_BLOCK_SAMPLES)) {
				k_mutex_unlock(&gAudioMutex);
				k_mem_slab_free(&gPdmSlab, buffer);
				failCapture(H1MicError::RingFrontier, -ENOSPC);
				goto next_start;
			}
			updateStatistics(gCaptureStatistics, samples, H1_MIC_BLOCK_SAMPLES,
					 gHavePreviousSample, gPreviousSample);
			windows().observe(ring().totalSamples());
			++gConsumedBlocks;
			++gExpectedSequence;
			const uint32_t slabUsed = k_mem_slab_num_used_get(&gPdmSlab);
			if (slabUsed > gSlabHighWater) {
				gSlabHighWater = slabUsed;
			}
			if (gMode != H1MicMode::CaptureOnly &&
			    windows().hasCompleteWindow()) {
				gState = H1MicState::WindowReady;
			}
			gLastDriverStatus = 0;
			k_mutex_unlock(&gAudioMutex);
			k_mem_slab_free(&gPdmSlab, buffer);
		}
		finishCaptureThread();
next_start:
		;
	}
}

int configurePdm()
{
	if (!device_is_ready(kPdm)) {
		return -ENODEV;
	}
	pcm_stream_cfg stream{};
	dmic_cfg config{};
	stream.pcm_width = 16;
	stream.mem_slab = &gPdmSlab;
	stream.block_size = H1_MIC_BLOCK_BYTES;
	config.streams = &stream;
	config.channel.req_num_streams = 1;
	config.channel.req_num_chan = 1;
	config.channel.req_chan_map_lo = kChannelMap;
	const int status = dmic_configure(kPdm, &config);
	if (status != 0) {
		return status;
	}

	pdm_set_ch_phase(kPdm, kChannel, kPhase);
	pdm_set_ch_gain(kPdm, kChannel, kGain);
	pdm_set_peak_detect_th(kPdm, kChannel, kPeakDetectThreshold);
	pdm_set_peak_detect_itv(kPdm, kChannel, kPeakDetectInterval);
	pdm_ch_config channel{};
	channel.ch_num = kChannel;
	memcpy(channel.ch_fir_coef, kFirCoefficients,
	       sizeof(channel.ch_fir_coef));
	channel.ch_iir_coef = kIirCoefficient;
	pdm_channel_config(kPdm, &channel);
	pdm_mode(kPdm, PDM_MODE_WIDE_BANDWIDTH_AUDIO_1536_CLK_FRQ);
	return 0;
}
} // namespace

extern "C" void h1PdmDriverTelemetryReset(void)
{
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	memset(&gDriverTelemetry, 0, sizeof(gDriverTelemetry));
	k_spin_unlock(&gDriverLock, key);
}

extern "C" void h1PdmDriverBlockPublished(uint32_t queueOccupancy)
{
	const uint64_t now = k_cycle_get_64();
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	if (gDriverTelemetry.published_blocks == 0) {
		gDriverTelemetry.first_publish_cycles = now;
	}
	++gDriverTelemetry.published_blocks;
	gDriverTelemetry.last_publish_cycles = now;
	gDriverTelemetry.queue_occupancy = queueOccupancy;
	if (queueOccupancy > gDriverTelemetry.queue_high_water) {
		gDriverTelemetry.queue_high_water = queueOccupancy;
	}
	k_spin_unlock(&gDriverLock, key);
}

extern "C" void h1PdmDriverFifoError(void)
{
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	++gDriverTelemetry.fifo_errors;
	k_spin_unlock(&gDriverLock, key);
}

extern "C" void h1PdmDriverSlabMiss(void)
{
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	++gDriverTelemetry.slab_misses;
	k_spin_unlock(&gDriverLock, key);
}

extern "C" void h1PdmDriverQueueOverrun(void)
{
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	++gDriverTelemetry.queue_overruns;
	k_spin_unlock(&gDriverLock, key);
}

extern "C" void h1PdmDriverTelemetryGet(H1PdmDriverTelemetry *telemetry)
{
	if (!telemetry) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&gDriverLock);
	*telemetry = gDriverTelemetry;
	k_spin_unlock(&gDriverLock, key);
}

int h1AudioPdmInit()
{
	if (gInitialized) {
		return gInitStatus;
	}
	gInitStatus = k_mem_slab_init(&gPdmSlab, gPdmSlabBuffer,
				      H1_MIC_BLOCK_BYTES, H1_MIC_SLAB_BLOCKS);
	if (gInitStatus == 0) {
		gInitStatus = configurePdm();
	}
	if (gInitStatus != 0) {
		gLastDriverStatus = gInitStatus;
		gLastError = H1MicError::NotInitialized;
		gState = H1MicState::Error;
		return gInitStatus;
	}
	ring().reset();
	windows().reset();
	resetStatistics(gCaptureStatistics);
	memset(&gSelectedWindow, 0, sizeof(gSelectedWindow));
	h1PdmDriverTelemetryReset();
	if (!gThreadCreated) {
		k_thread_create(&gCaptureThread, gCaptureStack,
				K_THREAD_STACK_SIZEOF(gCaptureStack), captureThread,
				nullptr, nullptr, nullptr, K_PRIO_COOP(1), 0,
				K_NO_WAIT);
		k_thread_name_set(&gCaptureThread, "h1_audio_producer");
		gThreadCreated = true;
	}
	gInitialized = true;
	gLastError = H1MicError::None;
	gState = H1MicState::Off;
	return 0;
}

bool h1AudioPdmStart(H1MicMode mode, H1MicError &error)
{
	error = H1MicError::None;
	if (!gInitialized) {
		error = H1MicError::NotInitialized;
		return false;
	}
	if (mode != H1MicMode::CaptureOnly &&
	    mode != H1MicMode::WindowValidate &&
	    mode != H1MicMode::H1Single) {
		error = H1MicError::InvalidMode;
		return false;
	}
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	if (gState != H1MicState::Off || atomic_get(&gThreadActive) != 0) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::AlreadyRunning;
		return false;
	}
	ring().reset();
	windows().reset();
	resetStatistics(gCaptureStatistics);
	gHavePreviousSample = false;
	gPreviousSample = 0;
	memset(&gSelectedWindow, 0, sizeof(gSelectedWindow));
	gConsumedBlocks = 0;
	gExpectedSequence = 0;
	gSequenceFaults = 0;
	gReadTimeouts = 0;
	gInvalidBlockSizes = 0;
	gSlabHighWater = 0;
	gMode = mode;
	gState = H1MicState::Starting;
	gInferenceState = H1MicInferenceState::Idle;
	gLastError = H1MicError::None;
	gLastDriverStatus = 0;
	k_mutex_unlock(&gAudioMutex);
	h1PdmDriverTelemetryReset();
	while (k_sem_take(&gCaptureStopped, K_NO_WAIT) == 0) {
	}
	atomic_set(&gRunRequested, 1);
	atomic_set(&gThreadActive, 1);
	const int status = dmic_trigger(kPdm, DMIC_TRIGGER_START);
	if (status != 0) {
		atomic_clear(&gRunRequested);
		atomic_clear(&gThreadActive);
		setStateError(H1MicError::TriggerStart, status);
		error = H1MicError::TriggerStart;
		return false;
	}
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	gState = H1MicState::Capture;
	k_mutex_unlock(&gAudioMutex);
	k_sem_give(&gCaptureStart);
	return true;
}

bool h1AudioPdmStop(H1MicError &error)
{
	error = H1MicError::None;
	if (!gInitialized) {
		error = H1MicError::NotInitialized;
		return false;
	}
	const bool active = atomic_get(&gThreadActive) != 0;
	atomic_clear(&gRunRequested);
	int status = 0;
	if (active) {
		status = dmic_trigger(kPdm, DMIC_TRIGGER_STOP);
		if (k_sem_take(&gCaptureStopped, K_MSEC(1500)) != 0 && status == 0) {
			status = -ETIMEDOUT;
		}
	}
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	if (status == 0) {
		gState = H1MicState::Off;
	} else {
		gState = H1MicState::Error;
		gLastError = H1MicError::TriggerStop;
		gLastDriverStatus = status;
		error = H1MicError::TriggerStop;
	}
	k_mutex_unlock(&gAudioMutex);
	return status == 0;
}

H1MicStatusSnapshot h1AudioPdmStatus()
{
	H1MicStatusSnapshot snapshot{};
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	snapshot.initialized = gInitialized;
	snapshot.running = atomic_get(&gRunRequested) != 0;
	snapshot.mode = gMode;
	snapshot.state = gState;
	snapshot.inferenceState = gInferenceState;
	snapshot.lastError = gLastError;
	snapshot.initStatus = gInitStatus;
	snapshot.lastDriverStatus = gLastDriverStatus;
	snapshot.consumedBlocks = gConsumedBlocks;
	snapshot.expectedSequence = gExpectedSequence;
	snapshot.sequenceFaults = gSequenceFaults;
	snapshot.readTimeouts = gReadTimeouts;
	snapshot.invalidBlockSizes = gInvalidBlockSizes;
	snapshot.slabUsed = k_mem_slab_num_used_get(&gPdmSlab);
	snapshot.slabHighWater = gSlabHighWater;
	snapshot.totalSamplesCommitted = ring().totalSamples();
	snapshot.oldestRetainedSample = ring().oldestRetainedSample();
	snapshot.ringWriteIndex = ring().writeIndex();
	snapshot.ringFrontierFaults = ring().frontierFaults();
	snapshot.completeWindows = windows().completeWindows();
	snapshot.latestWindowSequence = windows().latestSequence();
	snapshot.latestWindowStartSample = windows().latestStartSample();
	snapshot.latestWindowEndSample = windows().latestEndSample();
	snapshot.windowGeometryFaults = windows().geometryFaults();
	snapshot.captureStatistics = reportableStatistics(gCaptureStatistics);
	snapshot.selectedWindow = gSelectedWindow;
	k_mutex_unlock(&gAudioMutex);

	H1PdmDriverTelemetry driver{};
	h1PdmDriverTelemetryGet(&driver);
	snapshot.driverPublishedBlocks = driver.published_blocks;
	snapshot.fifoErrors = driver.fifo_errors;
	snapshot.slabMisses = driver.slab_misses;
	snapshot.driverQueueOverruns = driver.queue_overruns;
	snapshot.firstPublishCycles = driver.first_publish_cycles;
	snapshot.lastPublishCycles = driver.last_publish_cycles;
	snapshot.queueOccupancy =
		driver.published_blocks >= snapshot.consumedBlocks
			? uint32_t(driver.published_blocks - snapshot.consumedBlocks)
			: 0;
	snapshot.queueHighWater = driver.queue_high_water;
	snapshot.timingClockHz = uint32_t(sys_clock_hw_cycles_per_sec());
	return snapshot;
}

bool h1AudioPdmSelectWindow(uint64_t sequence, H1SelectedWindowInfo &info,
			    H1MicError &error)
{
	error = H1MicError::None;
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	if (gMode != H1MicMode::WindowValidate && gMode != H1MicMode::H1Single) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::ModeDisallowsOperation;
		return false;
	}
	if (!windows().hasCompleteWindow()) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::NoCompleteWindow;
		return false;
	}
	if (sequence == H1_MIC_LATEST_WINDOW) {
		sequence = windows().latestSequence();
	}
	uint64_t start = 0;
	uint64_t end = 0;
	if (!windows().bounds(sequence, start, end) ||
	    start < ring().oldestRetainedSample()) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::WindowNotRetained;
		return false;
	}
	H1SelectedWindowInfo selected{};
	selected.valid = true;
	selected.sequence = sequence;
	selected.startSample = start;
	selected.endSample = end;
	selected.physicalStartIndex = ring().physicalIndex(start);
	selected.wraps = ring().rangeWraps(start, H1_MIC_WINDOW_SAMPLES);
	ring().beginConsumer(start);
	const bool copied = ring().copyRange(start, H1_MIC_WINDOW_SAMPLES,
					     h1SelectedPcmWindow());
	ring().endConsumer();
	if (!copied) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::WindowNotRetained;
		return false;
	}
	gSelectedWindow.valid = false;
	k_mutex_unlock(&gAudioMutex);

	selected.crc32 = crc32(
		reinterpret_cast<const uint8_t *>(h1SelectedPcmWindow()),
		H1_MIC_WINDOW_BYTES);
	h1Sha256Hex(h1SelectedPcmWindow(), H1_MIC_WINDOW_BYTES,
		    selected.sha256);
	if (selected.sha256[0] == '\0') {
		error = H1MicError::ReadFailure;
		return false;
	}
	selected.statistics =
		calculateStatistics(h1SelectedPcmWindow(), H1_MIC_WINDOW_SAMPLES);

	k_mutex_lock(&gAudioMutex, K_FOREVER);
	gSelectedWindow = selected;
	info = selected;
	k_mutex_unlock(&gAudioMutex);
	return true;
}

bool h1AudioPdmCopySelectedBytes(uint32_t offsetBytes, void *destination,
				 uint32_t bytes, H1MicError &error)
{
	error = H1MicError::None;
	if (!destination || bytes == 0 || bytes > H1_MIC_PCM_CHUNK_MAX_BYTES ||
	    offsetBytes > H1_MIC_WINDOW_BYTES ||
	    bytes > H1_MIC_WINDOW_BYTES - offsetBytes) {
		error = H1MicError::WindowNotRetained;
		return false;
	}
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	if (!gSelectedWindow.valid) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::NoSelectedWindow;
		return false;
	}
	memcpy(destination,
	       reinterpret_cast<const uint8_t *>(h1SelectedPcmWindow()) + offsetBytes,
	       bytes);
	k_mutex_unlock(&gAudioMutex);
	return true;
}

bool h1AudioPdmPrepareSelectedWaveform(float *destination, size_t elements,
				       uint32_t &floatCrc32,
				       char floatSha256[65],
				       H1SelectedWindowInfo &info,
				       H1MicError &error)
{
	error = H1MicError::None;
	h1RunStateMark(H1RunState::MicWindowResolveBegin);
	if (!destination || !floatSha256 || elements != H1_MIC_WINDOW_SAMPLES) {
		error = H1MicError::InvalidBlockSize;
		return false;
	}
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	if (gMode != H1MicMode::H1Single) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::ModeDisallowsOperation;
		return false;
	}
	if (!gSelectedWindow.valid) {
		k_mutex_unlock(&gAudioMutex);
		error = H1MicError::NoSelectedWindow;
		return false;
	}
	h1RunStateSetWindow(gSelectedWindow.sequence);
	h1RunStateMark(H1RunState::MicWindowResolveDone);
	const int16_t *source = h1SelectedPcmWindow();
	h1RunStateMark(H1RunState::MicPcmToFloatBegin);
	for (size_t index = 0; index < elements; ++index) {
		destination[index] = float(source[index]) * (1.0f / 32768.0f);
	}
	const size_t bytes = elements * sizeof(float);
	floatCrc32 = crc32(reinterpret_cast<const uint8_t *>(destination), bytes);
	h1Sha256Hex(destination, bytes, floatSha256);
	info = gSelectedWindow;
	k_mutex_unlock(&gAudioMutex);
	h1RunStateSetWaveform(destination, elements, floatCrc32, floatSha256,
			      info.statistics.minimum, info.statistics.maximum);
	h1RunStateMark(H1RunState::MicPcmToFloatDone);
	return true;
}

void h1AudioPdmSetInferenceState(H1MicInferenceState state)
{
	k_mutex_lock(&gAudioMutex, K_FOREVER);
	gInferenceState = state;
	k_mutex_unlock(&gAudioMutex);
}

const char *h1MicModeName(H1MicMode mode)
{
	switch (mode) {
	case H1MicMode::Off:
		return "MIC_OFF";
	case H1MicMode::CaptureOnly:
		return "MIC_CAPTURE_ONLY";
	case H1MicMode::WindowValidate:
		return "MIC_WINDOW_VALIDATE";
	case H1MicMode::H1Single:
		return "MIC_H1_SINGLE";
	}
	return "MIC_MODE_UNKNOWN";
}

const char *h1MicStateName(H1MicState state)
{
	switch (state) {
	case H1MicState::Off:
		return "MIC_OFF";
	case H1MicState::Starting:
		return "MIC_STARTING";
	case H1MicState::Capture:
		return "MIC_CAPTURE";
	case H1MicState::WindowReady:
		return "MIC_WINDOW_READY";
	case H1MicState::Error:
		return "MIC_ERROR";
	}
	return "MIC_STATE_UNKNOWN";
}

const char *h1MicInferenceStateName(H1MicInferenceState state)
{
	switch (state) {
	case H1MicInferenceState::Idle:
		return "H1_IDLE";
	case H1MicInferenceState::Running:
		return "H1_RUNNING";
	case H1MicInferenceState::ResultReady:
		return "H1_RESULT_READY";
	}
	return "H1_STATE_UNKNOWN";
}

const char *h1MicErrorName(H1MicError error)
{
	switch (error) {
	case H1MicError::None:
		return "NONE";
	case H1MicError::NotInitialized:
		return "NOT_INITIALIZED";
	case H1MicError::AlreadyRunning:
		return "ALREADY_RUNNING";
	case H1MicError::InvalidMode:
		return "INVALID_MODE";
	case H1MicError::TriggerStart:
		return "TRIGGER_START";
	case H1MicError::TriggerStop:
		return "TRIGGER_STOP";
	case H1MicError::ReadTimeout:
		return "READ_TIMEOUT";
	case H1MicError::ReadFailure:
		return "READ_FAILURE";
	case H1MicError::InvalidBlockSize:
		return "INVALID_BLOCK_SIZE";
	case H1MicError::Fifo:
		return "PDM_FIFO";
	case H1MicError::SlabExhausted:
		return "SLAB_EXHAUSTED";
	case H1MicError::DriverQueueOverrun:
		return "DRIVER_QUEUE_OVERRUN";
	case H1MicError::Sequence:
		return "SEQUENCE";
	case H1MicError::RingFrontier:
		return "RING_FRONTIER";
	case H1MicError::NoCompleteWindow:
		return "NO_COMPLETE_WINDOW";
	case H1MicError::WindowNotRetained:
		return "WINDOW_NOT_RETAINED";
	case H1MicError::ModeDisallowsOperation:
		return "MODE_DISALLOWS_OPERATION";
	case H1MicError::NoSelectedWindow:
		return "NO_SELECTED_WINDOW";
	}
	return "UNKNOWN";
}
