#include "mic_usb.hpp"

#include "audio_contract.hpp"
#include "audio_pdm.hpp"
#include "model_storage.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t kPcmPayloadVersion = 1;
constexpr uint16_t kPcmFormatSignedLe16 = 1;
constexpr uint32_t kPcmHeaderBytes = 32;

uint32_t readLe32(const uint8_t *source)
{
	return uint32_t(source[0]) | (uint32_t(source[1]) << 8) |
	       (uint32_t(source[2]) << 16) | (uint32_t(source[3]) << 24);
}

uint64_t readLe64(const uint8_t *source)
{
	return uint64_t(readLe32(source)) |
	       (uint64_t(readLe32(source + 4)) << 32);
}

void writeLe16(uint8_t *destination, uint16_t value)
{
	destination[0] = uint8_t(value);
	destination[1] = uint8_t(value >> 8);
}

void writeLe32(uint8_t *destination, uint32_t value)
{
	destination[0] = uint8_t(value);
	destination[1] = uint8_t(value >> 8);
	destination[2] = uint8_t(value >> 16);
	destination[3] = uint8_t(value >> 24);
}

void writeLe64(uint8_t *destination, uint64_t value)
{
	writeLe32(destination, uint32_t(value));
	writeLe32(destination + 4, uint32_t(value >> 32));
}

bool sendJson(const H1UsbFrame &request, const char *json, size_t length)
{
	return h1UsbSendFrame(request.type | H1_CDC_RESPONSE_BIT, request.sequence,
			      json, uint32_t(length));
}

void sendError(const H1UsbFrame &request, const char *code, H1MicError error)
{
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":false,\"code\":\"%s\",\"mic_error\":\"%s\"}",
		code, h1MicErrorName(error));
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		h1UsbSendFrame(H1_CDC_ERROR_TYPE, request.sequence, response,
			       uint32_t(length));
	}
}

int64_t meanMicroPcm(const H1PcmStatistics &statistics)
{
	return statistics.sampleCount == 0
		? 0
		: (statistics.sum * INT64_C(1000000)) /
			  int64_t(statistics.sampleCount);
}

uint64_t rmsMicroPcm(const H1PcmStatistics &statistics)
{
	if (statistics.sampleCount == 0) {
		return 0;
	}
	const double meanSquare =
		double(statistics.sumSquares) / double(statistics.sampleCount);
	return uint64_t(std::sqrt(meanSquare) * 1000000.0 + 0.5);
}

uint64_t measuredRateMilliHz(const H1MicStatusSnapshot &status)
{
	if (status.driverPublishedBlocks < 2 ||
	    status.lastPublishCycles <= status.firstPublishCycles ||
	    status.timingClockHz == 0) {
		return 0;
	}
	const uint64_t samples =
		(status.driverPublishedBlocks - 1) * H1_MIC_BLOCK_SAMPLES;
	const uint64_t cycles =
		status.lastPublishCycles - status.firstPublishCycles;
	return uint64_t(double(samples) * double(status.timingClockHz) * 1000.0 /
				double(cycles) +
			0.5);
}

void sendMicStatus(const H1UsbFrame &request)
{
	const H1MicStatusSnapshot status = h1AudioPdmStatus();
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"mode\":\"%s\",\"state\":\"%s\","
		"\"inference_state\":\"%s\",\"initialized\":%s,"
		"\"running\":%s,\"last_error\":\"%s\","
		"\"total_samples_committed\":%llu,\"complete_windows\":%llu,"
		"\"latest_window_sequence\":%llu,\"queue_occupancy\":%u,"
		"\"queue_high_water\":%u,\"fifo_errors\":%llu,"
		"\"slab_misses\":%llu,\"queue_overruns\":%llu,"
		"\"sequence_faults\":%llu,\"ring_frontier_faults\":%llu,"
		"\"window_geometry_faults\":%llu}",
		h1MicModeName(status.mode), h1MicStateName(status.state),
		h1MicInferenceStateName(status.inferenceState),
		status.initialized ? "true" : "false",
		status.running ? "true" : "false",
		h1MicErrorName(status.lastError),
		(unsigned long long)status.totalSamplesCommitted,
		(unsigned long long)status.completeWindows,
		(unsigned long long)status.latestWindowSequence,
		status.queueOccupancy, status.queueHighWater,
		(unsigned long long)status.fifoErrors,
		(unsigned long long)status.slabMisses,
		(unsigned long long)status.driverQueueOverruns,
		(unsigned long long)status.sequenceFaults,
		(unsigned long long)status.ringFrontierFaults,
		(unsigned long long)status.windowGeometryFaults);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", H1MicError::ReadFailure);
	}
}

void sendCaptureInfo(const H1UsbFrame &request)
{
	const H1MicStatusSnapshot status = h1AudioPdmStatus();
	const H1PcmStatistics &s = status.captureStatistics;
	const uint64_t rateMilliHz = measuredRateMilliHz(status);
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"scope\":\"H1_ENGINEERING_DEVELOPMENT\","
		"\"acquisition_mechanism\":\"ALIF_PDM_IRQ_K_MEM_SLAB\","
		"\"dma_present\":false,\"dma_counters_applicability\":\"NOT_APPLICABLE\","
		"\"cache_transition\":\"CPU_ISR_TO_APPLICATION_NO_DMA_INVALIDATION\","
		"\"peripheral\":\"PDM@4902d000\",\"channel\":4,"
		"\"data_pin\":\"P5_4/PDM_D2_B\",\"clock_pin\":\"P6_7/PDM_C2_A\","
		"\"format\":\"SIGNED_PCM16_LE_MONO\",\"configured_rate_hz\":32000,"
		"\"clock_source\":\"ALIF_PDM_76M8_CLK\",\"clock_source_hz\":76800000,"
		"\"pdm_mode\":5,\"pdm_mode_name\":\"WIDE_BANDWIDTH_AUDIO_1536_CLK_FRQ\","
		"\"block_bytes\":%u,\"samples_per_block\":%u,\"block_count\":%u,"
		"\"driver_published_blocks\":%llu,\"consumed_blocks\":%llu,"
		"\"expected_sequence\":%llu,\"queue_occupancy\":%u,"
		"\"queue_high_water\":%u,\"slab_used\":%u,\"slab_high_water\":%u,"
		"\"fifo_errors\":%llu,\"slab_misses\":%llu,"
		"\"queue_overruns\":%llu,\"sequence_faults\":%llu,"
		"\"read_timeouts\":%llu,\"invalid_block_sizes\":%llu,"
		"\"ring_samples\":%u,\"ring_placement\":\"INTERNAL_SRAM1\","
		"\"ring_write_index\":%u,\"oldest_retained_sample\":%llu,"
		"\"total_samples_committed\":%llu,\"ring_frontier_faults\":%llu,"
		"\"window_samples\":%u,\"stride_samples\":%u,\"overlap_samples\":%u,"
		"\"complete_windows\":%llu,\"latest_window_sequence\":%llu,"
		"\"latest_window_start_sample\":%llu,\"latest_window_end_sample\":%llu,"
		"\"window_geometry_faults\":%llu,\"timing_clock_hz\":%u,"
		"\"first_publish_cycles\":%llu,\"last_publish_cycles\":%llu,"
		"\"measured_rate_millihz\":%llu,\"sample_count\":%llu,"
		"\"minimum\":%d,\"maximum\":%d,\"sum\":%lld,"
		"\"sum_squares\":%llu,\"mean_micro_pcm\":%lld,"
		"\"rms_micro_pcm\":%llu,\"positive_clipping_count\":%llu,"
		"\"negative_clipping_count\":%llu,\"zero_count\":%llu,"
		"\"maximum_adjacent_jump\":%u}",
		unsigned(H1_MIC_BLOCK_BYTES), unsigned(H1_MIC_BLOCK_SAMPLES),
		unsigned(H1_MIC_SLAB_BLOCKS),
		(unsigned long long)status.driverPublishedBlocks,
		(unsigned long long)status.consumedBlocks,
		(unsigned long long)status.expectedSequence,
		status.queueOccupancy, status.queueHighWater, status.slabUsed,
		status.slabHighWater, (unsigned long long)status.fifoErrors,
		(unsigned long long)status.slabMisses,
		(unsigned long long)status.driverQueueOverruns,
		(unsigned long long)status.sequenceFaults,
		(unsigned long long)status.readTimeouts,
		(unsigned long long)status.invalidBlockSizes,
		unsigned(H1_MIC_RING_SAMPLES), status.ringWriteIndex,
		(unsigned long long)status.oldestRetainedSample,
		(unsigned long long)status.totalSamplesCommitted,
		(unsigned long long)status.ringFrontierFaults,
		unsigned(H1_MIC_WINDOW_SAMPLES), unsigned(H1_MIC_STRIDE_SAMPLES),
		unsigned(H1_MIC_OVERLAP_SAMPLES),
		(unsigned long long)status.completeWindows,
		(unsigned long long)status.latestWindowSequence,
		(unsigned long long)status.latestWindowStartSample,
		(unsigned long long)status.latestWindowEndSample,
		(unsigned long long)status.windowGeometryFaults,
		status.timingClockHz,
		(unsigned long long)status.firstPublishCycles,
		(unsigned long long)status.lastPublishCycles,
		(unsigned long long)rateMilliHz,
		(unsigned long long)s.sampleCount, int(s.minimum), int(s.maximum),
		(long long)s.sum, (unsigned long long)s.sumSquares,
		(long long)meanMicroPcm(s), (unsigned long long)rmsMicroPcm(s),
		(unsigned long long)s.positiveClippingCount,
		(unsigned long long)s.negativeClippingCount,
		(unsigned long long)s.zeroCount, s.maximumAdjacentJump);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", H1MicError::ReadFailure);
	}
}

void sendWindowInfo(const H1UsbFrame &request)
{
	H1SelectedWindowInfo info{};
	H1MicError error = H1MicError::None;
	if (!h1AudioPdmSelectWindow(readLe64(request.payload), info, error)) {
		sendError(request, "WINDOW_SELECT_FAILED", error);
		return;
	}
	const H1PcmStatistics &s = info.statistics;
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"sequence\":%llu,\"start_sample\":%llu,"
		"\"end_sample\":%llu,\"sample_count\":%llu,\"bytes\":%u,"
		"\"physical_start_index\":%u,\"wraps\":%s,"
		"\"crc32\":\"%08x\",\"sha256\":\"%s\","
		"\"minimum\":%d,\"maximum\":%d,\"sum\":%lld,"
		"\"sum_squares\":%llu,\"mean_micro_pcm\":%lld,"
		"\"rms_micro_pcm\":%llu,\"positive_clipping_count\":%llu,"
		"\"negative_clipping_count\":%llu,\"zero_count\":%llu,"
		"\"maximum_adjacent_jump\":%u}",
		(unsigned long long)info.sequence,
		(unsigned long long)info.startSample,
		(unsigned long long)info.endSample,
		(unsigned long long)s.sampleCount, unsigned(H1_MIC_WINDOW_BYTES),
		info.physicalStartIndex, info.wraps ? "true" : "false",
		info.crc32, info.sha256, int(s.minimum), int(s.maximum),
		(long long)s.sum, (unsigned long long)s.sumSquares,
		(long long)meanMicroPcm(s), (unsigned long long)rmsMicroPcm(s),
		(unsigned long long)s.positiveClippingCount,
		(unsigned long long)s.negativeClippingCount,
		(unsigned long long)s.zeroCount, s.maximumAdjacentJump);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", H1MicError::ReadFailure);
	}
}

void sendWindowPcm(const H1UsbFrame &request)
{
	const uint32_t offset = readLe32(request.payload);
	const uint32_t bytes = readLe32(request.payload + 4);
	if (bytes == 0 || bytes > H1_MIC_PCM_CHUNK_MAX_BYTES) {
		sendError(request, "INVALID_PCM_RANGE",
			  H1MicError::WindowNotRetained);
		return;
	}
	const H1MicStatusSnapshot status = h1AudioPdmStatus();
	if (!status.selectedWindow.valid) {
		sendError(request, "NO_SELECTED_WINDOW",
			  H1MicError::NoSelectedWindow);
		return;
	}
	auto *payload = reinterpret_cast<uint8_t *>(h1ProtocolResponse());
	payload[0] = 'M';
	payload[1] = 'P';
	payload[2] = 'C';
	payload[3] = 'M';
	writeLe16(payload + 4, kPcmPayloadVersion);
	writeLe16(payload + 6, kPcmFormatSignedLe16);
	writeLe64(payload + 8, status.selectedWindow.sequence);
	writeLe32(payload + 16, offset);
	writeLe32(payload + 20, bytes);
	writeLe32(payload + 24, H1_MIC_WINDOW_BYTES);
	writeLe32(payload + 28, status.selectedWindow.crc32);
	H1MicError error = H1MicError::None;
	if (!h1AudioPdmCopySelectedBytes(offset, payload + kPcmHeaderBytes,
					 bytes, error)) {
		sendError(request, "PCM_COPY_FAILED", error);
		return;
	}
	h1UsbSendFrame(request.type | H1_CDC_RESPONSE_BIT, request.sequence,
		       payload, kPcmHeaderBytes + bytes);
}
} // namespace

bool h1MicUsbHandle(const H1UsbFrame &request)
{
	switch (H1MessageType(request.type)) {
	case H1MessageType::MicStatus:
		sendMicStatus(request);
		return true;
	case H1MessageType::MicStart: {
		const H1MicMode mode = H1MicMode(readLe32(request.payload));
		H1MicError error = H1MicError::None;
		if (!h1AudioPdmStart(mode, error)) {
			sendError(request, "MIC_START_FAILED", error);
			return true;
		}
		sendMicStatus(request);
		return true;
	}
	case H1MessageType::MicStop: {
		H1MicError error = H1MicError::None;
		if (!h1AudioPdmStop(error)) {
			sendError(request, "MIC_STOP_FAILED", error);
			return true;
		}
		sendMicStatus(request);
		return true;
	}
	case H1MessageType::MicCaptureInfo:
		sendCaptureInfo(request);
		return true;
	case H1MessageType::MicGetWindowInfo:
		sendWindowInfo(request);
		return true;
	case H1MessageType::MicGetWindowPcm:
		sendWindowPcm(request);
		return true;
	default:
		return false;
	}
}
