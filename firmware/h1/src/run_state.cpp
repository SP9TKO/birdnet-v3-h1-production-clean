#include "run_state.hpp"

#include "h1_contract.h"
#include "model_storage.hpp"
#include "runtime_profile.h"
#include "usb_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>

namespace {
constexpr uint32_t kMaximumEvents = 32;
constexpr uint16_t kRunStateResponseType =
	uint16_t(H1MessageType::GetRunState) | H1_CDC_RESPONSE_BIT;

struct H1RunEvent {
	H1RunState state;
	uint64_t cycles;
	uint32_t sinceCommandUs;
};

struct H1RunTrace {
	H1RunState current;
	H1RunState lastCompleted;
	uint32_t requestSequence;
	uint32_t clockHz;
	uint32_t activeWaveformCrc;
	uint64_t activeWindowSequence;
	uint64_t firstCycles;
	uint64_t stateEnterCycles;
	uintptr_t waveformBase;
	uintptr_t waveformEnd;
	uint32_t waveformBytes;
	uint32_t finiteCount;
	uint32_t minimumBits;
	uint32_t maximumBits;
	uint32_t firstBits[4];
	uint32_t lastBits[4];
	char waveformSha256[65];
	uint32_t eventCount;
	uint32_t txFailures;
	H1RunEvent events[kMaximumEvents];
};

H1RunTrace gRunTrace;

uint32_t bits(float value)
{
	uint32_t result = 0;
	memcpy(&result, &value, sizeof(result));
	return result;
}

bool completed(H1RunState state)
{
	switch (state) {
	case H1RunState::MicWindowResolveDone:
	case H1RunState::MicPcmToFloatDone:
	case H1RunState::FrontendDone:
	case H1RunState::BackbonePrepareDone:
	case H1RunState::BackboneInvokeDone:
	case H1RunState::GemDone:
	case H1RunState::ClassifierPrepareDone:
	case H1RunState::ClassifierInvokeDone:
	case H1RunState::PostprocessDone:
	case H1RunState::ResultReady:
	case H1RunState::UsbResponseDone:
		return true;
	default:
		return false;
	}
}

const char *name(H1RunState state)
{
	switch (state) {
	case H1RunState::None: return "NONE";
	case H1RunState::RunCommandReceived: return "RUN_COMMAND_RECEIVED";
	case H1RunState::MicRunCommandReceived: return "MIC_RUN_COMMAND_RECEIVED";
	case H1RunState::MicWindowResolveBegin: return "MIC_WINDOW_RESOLVE_BEGIN";
	case H1RunState::MicWindowResolveDone: return "MIC_WINDOW_RESOLVE_DONE";
	case H1RunState::MicPcmToFloatBegin: return "MIC_PCM_TO_FLOAT_BEGIN";
	case H1RunState::MicPcmToFloatDone: return "MIC_PCM_TO_FLOAT_DONE";
	case H1RunState::RunOnceEnter: return "RUNONCE_ENTER";
	case H1RunState::FrontendBegin: return "FRONTEND_BEGIN";
	case H1RunState::FrontendDone: return "FRONTEND_DONE";
	case H1RunState::BackbonePrepareBegin: return "BACKBONE_PREPARE_BEGIN";
	case H1RunState::BackbonePrepareDone: return "BACKBONE_PREPARE_DONE";
	case H1RunState::BackboneInvokeBegin: return "BACKBONE_INVOKE_BEGIN";
	case H1RunState::BackboneInvokeDone: return "BACKBONE_INVOKE_DONE";
	case H1RunState::GemBegin: return "GEM_BEGIN";
	case H1RunState::GemDone: return "GEM_DONE";
	case H1RunState::ClassifierPrepareBegin: return "CLASSIFIER_PREPARE_BEGIN";
	case H1RunState::ClassifierPrepareDone: return "CLASSIFIER_PREPARE_DONE";
	case H1RunState::ClassifierInvokeBegin: return "CLASSIFIER_INVOKE_BEGIN";
	case H1RunState::ClassifierInvokeDone: return "CLASSIFIER_INVOKE_DONE";
	case H1RunState::PostprocessBegin: return "POSTPROCESS_BEGIN";
	case H1RunState::PostprocessDone: return "POSTPROCESS_DONE";
	case H1RunState::ResultReady: return "RESULT_READY";
	case H1RunState::UsbResponseBegin: return "USB_RESPONSE_BEGIN";
	case H1RunState::UsbResponseDone: return "USB_RESPONSE_DONE";
	}
	return "UNKNOWN";
}
} // namespace

void h1RunStateBegin(uint32_t requestSequence, uint32_t waveformCrc)
{
	memset(&gRunTrace, 0, sizeof(gRunTrace));
	gRunTrace.requestSequence = requestSequence;
	gRunTrace.clockHz = sys_clock_hw_cycles_per_sec();
	gRunTrace.activeWaveformCrc = waveformCrc;
	gRunTrace.activeWindowSequence = UINT64_MAX;
	gRunTrace.firstCycles = h1ProfileNow();
}

void h1RunStateMark(H1RunState state)
{
	const uint64_t now = h1ProfileNow();
	gRunTrace.current = state;
	gRunTrace.stateEnterCycles = now;
	if (completed(state)) {
		gRunTrace.lastCompleted = state;
	}
	const uint32_t sinceCommandUs =
		h1ProfileCyclesToUs(now - gRunTrace.firstCycles, gRunTrace.clockHz);
	if (gRunTrace.eventCount < kMaximumEvents) {
		gRunTrace.events[gRunTrace.eventCount++] = {state, now, sinceCommandUs};
	}
	// Production compute tracing is memory-only. Transport is queried explicitly
	// through GetRunState so DSP/NPU execution never performs unsolicited USB I/O.
}

void h1RunStateSetWindow(uint64_t sequence)
{
	gRunTrace.activeWindowSequence = sequence;
}

void h1RunStateSetWaveform(const float *waveform, size_t elements,
			   uint32_t crc32, const char *sha256,
			   int16_t minimumPcm, int16_t maximumPcm)
{
	gRunTrace.waveformBase = reinterpret_cast<uintptr_t>(waveform);
	gRunTrace.waveformBytes = uint32_t(elements * sizeof(float));
	gRunTrace.waveformEnd = gRunTrace.waveformBase + gRunTrace.waveformBytes;
	gRunTrace.activeWaveformCrc = crc32;
	gRunTrace.finiteCount = uint32_t(elements);
	gRunTrace.minimumBits = bits(float(minimumPcm) * (1.0f / 32768.0f));
	gRunTrace.maximumBits = bits(float(maximumPcm) * (1.0f / 32768.0f));
	for (size_t i = 0; i < 4; ++i) {
		gRunTrace.firstBits[i] = bits(waveform[i]);
		gRunTrace.lastBits[i] = bits(waveform[elements - 4 + i]);
	}
	strncpy(gRunTrace.waveformSha256, sha256,
		sizeof(gRunTrace.waveformSha256) - 1);
}

bool h1RunStateSendSnapshot(uint32_t requestSequence)
{
	char *json = h1ProtocolResponse();
	const uint64_t now = h1ProfileNow();
	const uint64_t elapsed = now >= gRunTrace.stateEnterCycles
		? now - gRunTrace.stateEnterCycles : 0;
	int used = snprintf(json, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"current_state\":\"%s\","
		"\"last_completed_state\":\"%s\",\"state_enter_cycles\":%llu,"
		"\"elapsed_in_state_us\":%u,\"clock_hz\":%u,"
		"\"request_sequence\":%u,\"active_window_sequence\":%llu,"
		"\"active_waveform_crc\":\"%08x\","
		"\"waveform_base\":\"%08x\",\"waveform_end\":\"%08x\","
		"\"waveform_bytes\":%u,\"waveform_sha256\":\"%s\","
		"\"finite_count\":%u,\"minimum_bits\":\"%08x\","
		"\"maximum_bits\":\"%08x\",\"tx_failures\":%u,"
		"\"first_bits\":[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
		"\"last_bits\":[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
		"\"events\":[",
		name(gRunTrace.current), name(gRunTrace.lastCompleted),
		(unsigned long long)gRunTrace.stateEnterCycles,
		h1ProfileCyclesToUs(elapsed, gRunTrace.clockHz), gRunTrace.clockHz,
		gRunTrace.requestSequence,
		(unsigned long long)gRunTrace.activeWindowSequence,
		gRunTrace.activeWaveformCrc, unsigned(gRunTrace.waveformBase),
		unsigned(gRunTrace.waveformEnd), gRunTrace.waveformBytes,
		gRunTrace.waveformSha256, gRunTrace.finiteCount,
		gRunTrace.minimumBits, gRunTrace.maximumBits, gRunTrace.txFailures,
		gRunTrace.firstBits[0], gRunTrace.firstBits[1],
		gRunTrace.firstBits[2], gRunTrace.firstBits[3],
		gRunTrace.lastBits[0], gRunTrace.lastBits[1],
		gRunTrace.lastBits[2], gRunTrace.lastBits[3]);
	if (used < 0 || used >= int(H1_PROTOCOL_RESPONSE_BYTES)) return false;
	for (uint32_t i = 0; i < gRunTrace.eventCount; ++i) {
		const H1RunEvent &event = gRunTrace.events[i];
		const int added = snprintf(json + used, H1_PROTOCOL_RESPONSE_BYTES - used,
			"%s{\"state\":\"%s\",\"cycles\":%llu,\"us\":%u}",
			i == 0 ? "" : ",", name(event.state),
			(unsigned long long)event.cycles, event.sinceCommandUs);
		if (added < 0 || added >= int(H1_PROTOCOL_RESPONSE_BYTES - used)) return false;
		used += added;
	}
	const int added = snprintf(json + used, H1_PROTOCOL_RESPONSE_BYTES - used, "]}");
	if (added < 0 || added >= int(H1_PROTOCOL_RESPONSE_BYTES - used)) return false;
	used += added;
	return h1UsbSendFrame(kRunStateResponseType, requestSequence, json, uint32_t(used));
}
