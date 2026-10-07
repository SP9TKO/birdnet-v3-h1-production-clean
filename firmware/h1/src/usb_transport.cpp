#include "usb_transport.hpp"

#include "h1_contract.h"
#include "model_storage.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usbd.h>

namespace {
constexpr uint16_t kVid = 0x2fe3;
constexpr uint16_t kPid = 0x0001;
constexpr uint8_t kMagic[4] = {'H', '1', 'C', 'P'};
constexpr uint32_t kSmallPayloadBytes = 512;
constexpr uint32_t kMaximumResponseBytes = 7000;

USBD_DEVICE_DEFINE(h1Usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), kVid, kPid);
USBD_DESC_LANG_DEFINE(h1Lang);
USBD_DESC_MANUFACTURER_DEFINE(h1Manufacturer, "BirdNET Clean Reproduction");
USBD_DESC_PRODUCT_DEFINE(h1Product, "BirdNET H1 Dev CDC");
USBD_DESC_STRING_DEFINE(h1Serial, "H1DEV1", USBD_DUT_STRING_SERIAL_NUMBER);
USBD_DESC_CONFIG_DEFINE(h1FsConfigDescription, "H1 CDC full-speed configuration");
USBD_DESC_CONFIG_DEFINE(h1HsConfigDescription, "H1 CDC high-speed configuration");
USBD_CONFIGURATION_DEFINE(h1FsConfig, 0, 50, &h1FsConfigDescription);
USBD_CONFIGURATION_DEFINE(h1HsConfig, 0, 50, &h1HsConfigDescription);

const char *const kClassBlocklist[] = {"dfu_dfu", nullptr};
const device *const kCdc = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

atomic_t gInitialized;
atomic_t gEnabled;
atomic_t gVbusPresent;
atomic_t gConfigured;
atomic_t gDtr;
atomic_t gGeneration;
atomic_t gReceivedBytes;
atomic_t gTransmittedBytes;
atomic_t gLastStackError;
int32_t gInitStatus = -EAGAIN;

uint8_t gHeader[H1_CDC_HEADER_BYTES];
uint8_t gSmallPayload[kSmallPayloadBytes];
uint32_t gHeaderCount;
uint32_t gPayloadCount;
uint32_t gPayloadLength;
uint32_t gPayloadCrc;
uint32_t gSequence;
uint16_t gType;
uint8_t *gPayload;
uint32_t gObservedGeneration;

uint16_t readLe16(const uint8_t *source)
{
	return uint16_t(source[0]) | uint16_t(uint16_t(source[1]) << 8);
}

uint32_t readLe32(const uint8_t *source)
{
	return uint32_t(source[0]) | (uint32_t(source[1]) << 8) |
	       (uint32_t(source[2]) << 16) | (uint32_t(source[3]) << 24);
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

void resetParser(bool completedUpload = false)
{
	if (completedUpload) {
		h1WaveformLifecycle.parserActive = 0;
	} else {
		h1WaveformUploadAttemptAbort();
	}
	h1WaveformLifecycle.parserDenied = 0;
	gHeaderCount = 0;
	gPayloadCount = 0;
	gPayloadLength = 0;
	gPayloadCrc = 0;
	gSequence = 0;
	gType = 0;
	gPayload = nullptr;
}

void fixCodeTriple(enum usbd_speed speed)
{
	usbd_device_set_code_triple(&h1Usbd, speed, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

int setupDevice()
{
	int error = usbd_add_descriptor(&h1Usbd, &h1Lang);
	error |= usbd_add_descriptor(&h1Usbd, &h1Manufacturer);
	error |= usbd_add_descriptor(&h1Usbd, &h1Product);
	error |= usbd_add_descriptor(&h1Usbd, &h1Serial);
	if (error != 0) {
		return error;
	}
	if (usbd_caps_speed(&h1Usbd) == USBD_SPEED_HS) {
		error = usbd_add_configuration(&h1Usbd, USBD_SPEED_HS, &h1HsConfig);
		if (error != 0) {
			return error;
		}
		error = usbd_register_all_classes(&h1Usbd, USBD_SPEED_HS, 1,
					  kClassBlocklist);
		if (error != 0) {
			return error;
		}
		fixCodeTriple(USBD_SPEED_HS);
	}
	error = usbd_add_configuration(&h1Usbd, USBD_SPEED_FS, &h1FsConfig);
	if (error != 0) {
		return error;
	}
	error = usbd_register_all_classes(&h1Usbd, USBD_SPEED_FS, 1,
					  kClassBlocklist);
	if (error != 0) {
		return error;
	}
	fixCodeTriple(USBD_SPEED_FS);
	usbd_self_powered(&h1Usbd, false);
	return 0;
}

void messageCallback(usbd_context *const context, const usbd_msg *const message)
{
	switch (message->type) {
	case USBD_MSG_VBUS_READY: {
		atomic_set(&gVbusPresent, 1);
		const int error = usbd_enable(context);
		if (error == 0 || error == -EALREADY) {
			atomic_set(&gEnabled, 1);
		} else {
			atomic_set(&gLastStackError, error);
		}
		break;
	}
	case USBD_MSG_VBUS_REMOVED: {
		atomic_set(&gVbusPresent, 0);
		atomic_set(&gConfigured, 0);
		atomic_set(&gDtr, 0);
		atomic_inc(&gGeneration);
		const int error = usbd_disable(context);
		if (error == 0 || error == -EALREADY) {
			atomic_set(&gEnabled, 0);
		} else {
			atomic_set(&gLastStackError, error);
		}
		break;
	}
	case USBD_MSG_RESET:
		atomic_set(&gConfigured, 0);
		atomic_set(&gDtr, 0);
		atomic_inc(&gGeneration);
		break;
	case USBD_MSG_CONFIGURATION:
		atomic_set(&gConfigured, message->status != 0);
		if (message->status == 0) {
			atomic_inc(&gGeneration);
			atomic_set(&gDtr, 0);
		}
		break;
	case USBD_MSG_CDC_ACM_CONTROL_LINE_STATE: {
		uint32_t dtr = 0;
		if (uart_line_ctrl_get(message->dev, UART_LINE_CTRL_DTR, &dtr) == 0) {
			if (dtr == 0 && atomic_get(&gDtr) != 0) atomic_inc(&gGeneration);
			atomic_set(&gDtr, dtr != 0);
		}
		break;
	}
	case USBD_MSG_UDC_ERROR:
	case USBD_MSG_STACK_ERROR:
		atomic_set(&gLastStackError, message->status);
		break;
	default:
		break;
	}
}

bool validPayloadLength(uint16_t type, uint32_t length)
{
	switch (H1MessageType(type)) {
	case H1MessageType::UploadWaveform:
		return length == H1_UPLOAD_PAYLOAD_BYTES;
	case H1MessageType::MicStart:
	case H1MessageType::ArmRead:
	case H1MessageType::RunM55CompleteFrontend:
		return length == sizeof(uint32_t) || length == 2 * sizeof(uint32_t);
	case H1MessageType::GetBaselineSample:
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
	case H1MessageType::GetHotPathValidationObservation:
#endif
#if defined(H1_POSTPROCESSING_OBSERVATION)
	case H1MessageType::GetPostprocessObservation:
#endif
	case H1MessageType::RunM55SpectralLoop:
	case H1MessageType::RunM55SpectralBoundedLoop:
		return length == sizeof(uint32_t);
	case H1MessageType::RunM55SpectralFrame:
		return length == sizeof(uint32_t) || length == 2 * sizeof(uint32_t);
	case H1MessageType::MicGetWindowInfo:
	case H1MessageType::MicGetWindowPcm:
		return length == sizeof(uint64_t);
	case H1MessageType::I2sSelectChannel:
		return length == 1;
	case H1MessageType::I2sRawRead:
	case H1MessageType::I2sWindowRead:
		return length == 2 * sizeof(uint32_t);
	case H1MessageType::GetM55SpectralData:
		return length == 3 * sizeof(uint32_t);
	case H1MessageType::GetInferenceData:
	case H1MessageType::GetM55FrontendData:
		return length == 3 * sizeof(uint32_t);
	case H1MessageType::RunBaselineObserverQualification:
	case H1MessageType::RunBaselineAcceptance:
	case H1MessageType::RunBaselineDiagnostic:
	case H1MessageType::RunBaselinePmu:
	case H1MessageType::GetWaveformLifecycle:
	case H1MessageType::GetBaselineStatus:
	case H1MessageType::GetBaselineMap:
	case H1MessageType::Ping:
	case H1MessageType::Status:
	case H1MessageType::GetIdentity:
	case H1MessageType::VerifyModelStorage:
	case H1MessageType::CompareNumericErrorSelfTest:
	case H1MessageType::CompareNativeSpectral:
	case H1MessageType::CompareCompactScalarMel:
	case H1MessageType::CompareMveCompactMel:
	case H1MessageType::RunLegacyUploadedWaveform:
	case H1MessageType::RunUploadedWaveform:
	case H1MessageType::RunCanonical:
	case H1MessageType::GetTopK:
	case H1MessageType::GetProfile:
	case H1MessageType::GetResultSummary:
	case H1MessageType::MicStatus:
	case H1MessageType::MicStop:
	case H1MessageType::MicCaptureInfo:
	case H1MessageType::MicRunWindow:
	case H1MessageType::GetRunState:
	case H1MessageType::GetM55SpectralDiagnostic:
	case H1MessageType::GetM55SpectralTiming:
	case H1MessageType::GetM55FrontendExecution:
	case H1MessageType::MarkOnly:
	case H1MessageType::ExecuteRead:
	case H1MessageType::PsramReady:
	case H1MessageType::I2sStatus:
	case H1MessageType::I2sStart:
	case H1MessageType::I2sWindowInfo:
	case H1MessageType::I2sStop:
	case H1MessageType::I2sWindowHistory:
	case H1MessageType::I2sConfigure:
	case H1MessageType::I2sStartSingle:
		return length == 0;
	}
	return false;
}

H1UsbPollResult headerComplete(H1UsbFrame &frame)
{
	const uint16_t version = readLe16(gHeader + 4);
	gType = readLe16(gHeader + 6);
	gPayloadLength = readLe32(gHeader + 8);
	gSequence = readLe32(gHeader + 12);
	gPayloadCrc = readLe32(gHeader + 16);
	frame.type = gType;
	frame.sequence = gSequence;
	frame.payloadLength = gPayloadLength;
	frame.payload = nullptr;
	frame.expectedPayloadCrc = gPayloadCrc;
	frame.actualPayloadCrc = 0;
	if (version != H1_CDC_PROTOCOL_VERSION) {
		frame.error = H1UsbProtocolError::UnsupportedVersion;
		resetParser();
		return H1UsbPollResult::ProtocolError;
	}
	if (!validPayloadLength(gType, gPayloadLength)) {
		frame.error = H1UsbProtocolError::InvalidLength;
		resetParser();
		return H1UsbPollResult::ProtocolError;
	}
	if (gType == uint16_t(H1MessageType::UploadWaveform) &&
	    (!h1WaveformLifecycle.parserActive || h1WaveformLifecycle.parserDenied)) {
		frame.error = H1UsbProtocolError::WaveformOwnershipDenied;
		resetParser();
		return H1UsbPollResult::ProtocolError;
	}
	gPayload = gType == uint16_t(H1MessageType::UploadWaveform)
		? h1UploadPayload()
		: gSmallPayload;
	if (gPayloadLength == 0) {
		frame.actualPayloadCrc = 0;
		if (gPayloadCrc != 0) {
			frame.error = H1UsbProtocolError::PayloadCrcMismatch;
			resetParser();
			return H1UsbPollResult::ProtocolError;
		}
		frame.error = H1UsbProtocolError::None;
		frame.payload = gPayload;
		resetParser();
		return H1UsbPollResult::Frame;
	}
	return H1UsbPollResult::None;
}
} // namespace

int h1UsbInit()
{
	if (!device_is_ready(kCdc) || !device_is_ready(DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)))) {
		gInitStatus = -ENODEV;
		return gInitStatus;
	}
	int error = setupDevice();
	if (error == 0) {
		error = usbd_msg_register_cb(&h1Usbd, messageCallback);
	}
	if (error == 0) {
		error = usbd_init(&h1Usbd);
	}
	if (error == 0) {
		/*
		 * The pinned next-stack CDC driver starts its first bulk-OUT
		 * request only when RX is enabled. Polling remains the application
		 * read API; this call merely arms the driver's receive engine and
		 * does not install an IRQ callback or wait for a host/DTR.
		 */
		uart_irq_rx_enable(kCdc);
	}
	if (error == 0 && !usbd_can_detect_vbus(&h1Usbd)) {
		error = usbd_enable(&h1Usbd);
		if (error == 0) {
			atomic_set(&gEnabled, 1);
		}
	}
	gInitStatus = error;
	if (error == 0) {
		atomic_set(&gInitialized, 1);
		gObservedGeneration = uint32_t(atomic_get(&gGeneration));
		resetParser();
	}
	return error;
}

H1UsbPollResult h1UsbPoll(H1UsbFrame &frame)
{
	const uint32_t generation = uint32_t(atomic_get(&gGeneration));
	if (generation != gObservedGeneration) {
		resetParser();
		gObservedGeneration = generation;
	}
	unsigned char byte = 0;
	while (uart_poll_in(kCdc, &byte) == 0) {
		atomic_inc(&gReceivedBytes);
		if (gHeaderCount < 4) {
			if (byte == kMagic[gHeaderCount]) {
				gHeader[gHeaderCount++] = byte;
			} else {
				gHeaderCount = byte == kMagic[0] ? 1u : 0u;
				if (gHeaderCount == 1) {
					gHeader[0] = byte;
				}
			}
			continue;
		}
		if (gHeaderCount < H1_CDC_HEADER_BYTES) {
			gHeader[gHeaderCount++] = byte;
			// Type is recognizable at byte 8, even for a bad version/length.
			if (gHeaderCount == 8 &&
			    readLe16(gHeader + 6) == uint16_t(H1MessageType::UploadWaveform)) {
				h1WaveformLifecycle.parserDenied = !h1WaveformUploadAttemptBegin();
			}
			if (gHeaderCount == H1_CDC_HEADER_BYTES) {
				const H1UsbPollResult result = headerComplete(frame);
				if (result != H1UsbPollResult::None) {
					return result;
				}
			}
			continue;
		}
		gPayload[gPayloadCount++] = byte;
		if (gPayloadCount == gPayloadLength) {
			frame.type = gType;
			frame.sequence = gSequence;
			frame.payloadLength = gPayloadLength;
			frame.payload = gPayload;
			frame.expectedPayloadCrc = gPayloadCrc;
			frame.actualPayloadCrc = crc32(gPayload, gPayloadLength);
			frame.error = frame.actualPayloadCrc == gPayloadCrc
				? H1UsbProtocolError::None
				: H1UsbProtocolError::PayloadCrcMismatch;
			const H1UsbPollResult result = frame.error == H1UsbProtocolError::None
				? H1UsbPollResult::Frame
				: H1UsbPollResult::ProtocolError;
			const bool uploadComplete = result == H1UsbPollResult::Frame &&
				gType == uint16_t(H1MessageType::UploadWaveform);
			if (uploadComplete) frame.waveformToken = h1WaveformLifecycle.parserToken;
			resetParser(uploadComplete);
			return result;
		}
	}
	return H1UsbPollResult::None;
}

bool h1UsbSendFrame(uint16_t type, uint32_t sequence, const void *payload,
		    uint32_t payloadLength)
{
	if (payloadLength > kMaximumResponseBytes ||
	    (payloadLength != 0 && payload == nullptr) ||
	    atomic_get(&gConfigured) == 0) {
		return false;
	}
	uint8_t header[H1_CDC_HEADER_BYTES] = {'H', '1', 'C', 'P'};
	writeLe16(header + 4, H1_CDC_PROTOCOL_VERSION);
	writeLe16(header + 6, type);
	writeLe32(header + 8, payloadLength);
	writeLe32(header + 12, sequence);
	writeLe32(header + 16,
		  crc32(static_cast<const uint8_t *>(payload), payloadLength));
	for (uint8_t byte : header) {
		uart_poll_out(kCdc, byte);
	}
	const auto *bytes = static_cast<const uint8_t *>(payload);
	for (uint32_t index = 0; index < payloadLength; ++index) {
		uart_poll_out(kCdc, bytes[index]);
	}
	atomic_add(&gTransmittedBytes, H1_CDC_HEADER_BYTES + payloadLength);
	return true;
}


// Only the completed baseline diagnostic export calls this streaming sender.
// Its two bounded stack headers avoid another persistent response/page buffer.
bool h1UsbSendDiagnosticPages(uint16_t type, uint32_t sequence, const void *payload,
                              uint32_t payloadLength)
{
 constexpr uint32_t pageHeaderBytes = 40;
 constexpr uint32_t pageDataBytes = 6940;
 static_assert(H1_CDC_HEADER_BYTES + pageHeaderBytes + pageDataBytes == 7000);
 static_assert(pageHeaderBytes + pageDataBytes <= kMaximumResponseBytes);
 if (!payload || payloadLength == 0 || payloadLength >= 16384 ||
     type != (uint16_t(H1MessageType::GetBaselineSample) | H1_CDC_RESPONSE_BIT))
  return false;
 const auto *bytes = static_cast<const uint8_t *>(payload);
 const uint32_t wholeCrc = crc32(bytes, payloadLength);
 const uint32_t pages = (payloadLength + pageDataBytes - 1) / pageDataBytes;
 for (uint32_t index = 0; index < pages; ++index) {
  if (atomic_get(&gConfigured) == 0) return false;
  const uint32_t offset = index * pageDataBytes;
  const uint32_t count = payloadLength - offset < pageDataBytes ?
   payloadLength - offset : pageDataBytes;
  uint8_t page[pageHeaderBytes] = {'H', '1', 'D', 'P'};
  writeLe16(page + 4, 1); writeLe16(page + 6, pageHeaderBytes);
  writeLe32(page + 8, sequence); writeLe32(page + 12, index);
  writeLe32(page + 16, pages); writeLe32(page + 20, offset);
  writeLe32(page + 24, count); writeLe32(page + 28, payloadLength);
  writeLe32(page + 32, crc32(bytes + offset, count));
  writeLe32(page + 36, wholeCrc);
  uint32_t frameCrc = UINT32_C(0xffffffff);
  const auto accumulate = [&frameCrc](const uint8_t *data, uint32_t length) {
   for (uint32_t i = 0; i < length; ++i) {
    frameCrc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
     frameCrc = (frameCrc >> 1) ^
      (UINT32_C(0xedb88320) & (0u - (frameCrc & 1u)));
   }
  };
  accumulate(page, pageHeaderBytes); accumulate(bytes + offset, count);
  uint8_t header[H1_CDC_HEADER_BYTES] = {'H', '1', 'C', 'P'};
  writeLe16(header + 4, H1_CDC_PROTOCOL_VERSION); writeLe16(header + 6, type);
  writeLe32(header + 8, pageHeaderBytes + count); writeLe32(header + 12, sequence);
  writeLe32(header + 16, ~frameCrc);
  for (uint8_t byte : header) uart_poll_out(kCdc, byte);
  for (uint8_t byte : page) uart_poll_out(kCdc, byte);
  for (uint32_t i = 0; i < count; ++i) uart_poll_out(kCdc, bytes[offset + i]);
  atomic_add(&gTransmittedBytes, H1_CDC_HEADER_BYTES + pageHeaderBytes + count);
 }
 return true;
}

H1UsbStatus h1UsbGetStatus()
{
	return {
		gInitStatus,
		int32_t(atomic_get(&gLastStackError)),
		uint32_t(atomic_get(&gInitialized)),
		uint32_t(atomic_get(&gEnabled)),
		uint32_t(atomic_get(&gVbusPresent)),
		uint32_t(atomic_get(&gConfigured)),
		uint32_t(atomic_get(&gDtr)),
		uint32_t(usbd_bus_speed(&h1Usbd)),
		uint32_t(usbd_caps_speed(&h1Usbd)),
		uint32_t(atomic_get(&gGeneration)),
		uint32_t(atomic_get(&gReceivedBytes)),
		uint32_t(atomic_get(&gTransmittedBytes)),
	};
}

const char *h1UsbSpeedName(uint32_t speed)
{
	switch (speed) {
	case USBD_SPEED_FS:
		return "full-speed";
	case USBD_SPEED_HS:
		return "high-speed";
	default:
		return "unknown";
	}
}
