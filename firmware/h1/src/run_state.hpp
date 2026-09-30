#pragma once

#include <cstddef>
#include <cstdint>

enum class H1RunState : uint8_t {
	None,
	RunCommandReceived,
	MicRunCommandReceived,
	MicWindowResolveBegin,
	MicWindowResolveDone,
	MicPcmToFloatBegin,
	MicPcmToFloatDone,
	RunOnceEnter,
	FrontendBegin,
	FrontendDone,
	BackbonePrepareBegin,
	BackbonePrepareDone,
	BackboneInvokeBegin,
	BackboneInvokeDone,
	GemBegin,
	GemDone,
	ClassifierPrepareBegin,
	ClassifierPrepareDone,
	ClassifierInvokeBegin,
	ClassifierInvokeDone,
	PostprocessBegin,
	PostprocessDone,
	ResultReady,
	UsbResponseBegin,
	UsbResponseDone,
};

void h1RunStateBegin(uint32_t requestSequence, uint32_t waveformCrc);
void h1RunStateMark(H1RunState state);
void h1RunStateSetWindow(uint64_t sequence);
void h1RunStateSetWaveform(const float *waveform, size_t elements,
			   uint32_t crc32, const char *sha256,
			   int16_t minimumPcm, int16_t maximumPcm);
bool h1RunStateSendSnapshot(uint32_t requestSequence);
