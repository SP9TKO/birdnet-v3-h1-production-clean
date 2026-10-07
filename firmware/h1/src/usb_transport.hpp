#pragma once

#include <cstddef>
#include <cstdint>
#include "waveform_lifecycle.hpp"

constexpr uint16_t H1_CDC_PROTOCOL_VERSION = 1;
constexpr size_t H1_CDC_HEADER_BYTES = 20;
constexpr uint16_t H1_CDC_RESPONSE_BIT = 0x8000;
constexpr uint16_t H1_CDC_ERROR_TYPE = 0xffff;

enum class H1MessageType : uint16_t {
	Ping = 1,
	Status = 2,
	GetIdentity = 3,
	UploadWaveform = 4,
	RunUploadedWaveform = 5,
	RunCanonical = 6,
	GetTopK = 7,
	GetProfile = 8,
	GetResultSummary = 9,
	MicStatus = 10,
	MicStart = 11,
	MicStop = 12,
	MicCaptureInfo = 13,
	MicGetWindowInfo = 14,
	MicGetWindowPcm = 15,
	MicRunWindow = 16,
	GetRunState = 17,
	RunM55SpectralFrame = 18,
	GetM55SpectralData = 19,
	GetM55SpectralDiagnostic = 20,
	GetM55SpectralTiming = 21,
	MarkOnly = 22,
	ArmRead = 23,
	ExecuteRead = 24,
	PsramReady = 25,
	I2sStatus = 26,
	I2sStart = 27,
	I2sSelectChannel = 28,
	I2sRawRead = 29,
	I2sWindowInfo = 30,
	I2sWindowRead = 31,
	I2sStop = 32,
	I2sConfigure = 33,
	I2sStartSingle = 34,
	I2sWindowHistory = 35,
	RunM55SpectralLoop = 36,
	RunM55CompleteFrontend = 37,
	RunM55SpectralBoundedLoop = 38,
	GetM55FrontendData = 39,
	GetM55FrontendExecution = 40,
	VerifyModelStorage = 41,
	CompareNumericErrorSelfTest = 45,
	CompareNativeSpectral = 46,
	CompareCompactScalarMel = 47,
	CompareMveCompactMel = 48,
	RunLegacyUploadedWaveform = 49,
	GetInferenceData = 50,
	RunBaselineAcceptance = 144,
	RunBaselineDiagnostic = 145,
	RunBaselinePmu = 146,
	GetBaselineSample = 147,
	GetBaselineStatus = 148,
	GetBaselineMap = 149,
	RunBaselineObserverQualification = 150,
	GetPostprocessObservation = 151,
	GetHotPathValidationObservation = 152,
	GetWaveformLifecycle = 153,
};

enum class H1UsbPollResult : uint8_t {
	None,
	Frame,
	ProtocolError,
};

enum class H1UsbProtocolError : uint32_t {
	None = 0,
	UnsupportedVersion = 1,
	InvalidLength = 2,
	PayloadCrcMismatch = 3,
	WaveformOwnershipDenied = 4,
};

struct H1UsbFrame {
	uint16_t type;
	uint32_t sequence;
	uint32_t payloadLength;
	uint8_t *payload;
	H1UsbProtocolError error;
	uint32_t expectedPayloadCrc;
	uint32_t actualPayloadCrc;
	H1WaveformToken waveformToken;
};

struct H1UsbStatus {
	int32_t initStatus;
	int32_t lastStackError;
	uint32_t initialized;
	uint32_t enabled;
	uint32_t vbusPresent;
	uint32_t configured;
	uint32_t dtr;
	uint32_t busSpeed;
	uint32_t controllerMaximumSpeed;
	uint32_t connectionGeneration;
	uint32_t receivedBytes;
	uint32_t transmittedBytes;
};

int h1UsbInit();
H1UsbPollResult h1UsbPoll(H1UsbFrame &frame);
bool h1UsbSendFrame(uint16_t type, uint32_t sequence, const void *payload,
		    uint32_t payloadLength);
// Diagnostic export only; ordinary H1CP framing and limits remain unchanged.
bool h1UsbSendDiagnosticPages(uint16_t type, uint32_t sequence, const void *payload,
                              uint32_t payloadLength);
H1UsbStatus h1UsbGetStatus();
const char *h1UsbSpeedName(uint32_t speed);
