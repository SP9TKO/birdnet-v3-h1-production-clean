#include "model_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <flatbuffers/verifier.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

namespace {
constexpr size_t kGuardBytes = 64;

struct alignas(32) H1PsramStorage {
	uint8_t model[H1_MODEL_SLOT_BYTES];
	alignas(32) uint8_t modelGuard[kGuardBytes];
	alignas(32) H1Boundaries current;
	alignas(32) H1Boundaries first;
	alignas(32) H1FrontendScratch frontendScratch;
	alignas(32) H1M55SpectralDiagnostics spectralDiagnostics;
	alignas(32) uint8_t uploadPayload[H1_UPLOAD_PAYLOAD_BYTES];
	alignas(32) float uploadedWaveform[H1_WAVEFORM_ELEMENTS];
};

__attribute__((section("SRAM1.protocol"), aligned(32)))
char protocolResponse[H1_PROTOCOL_RESPONSE_BYTES];

__attribute__((section("SRAM1.audio_ring"), aligned(32)))
int16_t pcmRing[H1_MIC_RING_SAMPLES];

__attribute__((section("SRAM1.audio_window"), aligned(32)))
int16_t selectedPcmWindow[H1_MIC_WINDOW_SAMPLES];

extern "C" {
__attribute__((section("PSRAM_MODEL.model"), aligned(32)))
H1PsramStorage h1PsramStorage;
}

/* Owned by the SRAM1 linker region; keep the hot FFT workspace off PSRAM. */
__attribute__((section("SRAM1.spectral_workspace"), aligned(32)))
H1M55SpectralWorkspace h1SpectralWorkspace;

/* Exact frozen mel weights copied into a compact SRAM1 span table. */
__attribute__((section("SRAM1.zz_mel_weights"), aligned(32)))
H1M55CompactMel h1CompactMel;

static_assert(offsetof(H1PsramStorage, model) == 0);
static_assert(sizeof(H1PsramStorage) < H1_PSRAM_END - H1_PSRAM_BASE);

uint32_t crc32(const uint8_t *data, size_t bytes)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t index = 0; index < bytes; ++index) {
		crc ^= data[index];
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

uint8_t guardPattern(unsigned index)
{
	return static_cast<uint8_t>(0x69u ^ (index * 43u));
}

const char *stageName(H1NpuStage stage)
{
	return stage == H1NpuStage::Backbone ? "backbone" : "classifier";
}

bool withinModel(const void *pointer, size_t bytes, size_t modelBytes)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(h1PsramStorage.model);
	const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
	return address >= base && bytes <= modelBytes && address - base <= modelBytes - bytes;
}
} // namespace

const tflite::Model *h1PrepareNpuModel(H1NpuStage stage,
					       H1ModelLifecycleProfile &profile,
					       bool diagnostics)
{
	const uintptr_t sourceAddress = stage == H1NpuStage::Backbone
						 ? H1_BACKBONE_FLASH_ADDRESS
						 : H1_CLASSIFIER_FLASH_ADDRESS;
	const size_t modelBytes = stage == H1NpuStage::Backbone
					 ? H1_BACKBONE_MODEL_BYTES
					 : H1_CLASSIFIER_MODEL_BYTES;
	const uint32_t expectedCrc = stage == H1NpuStage::Backbone
						 ? H1_BACKBONE_MODEL_CRC32
						 : H1_CLASSIFIER_MODEL_CRC32;
	const auto *source = reinterpret_cast<const uint8_t *>(sourceAddress);
	const device *ram = DEVICE_DT_GET(DT_NODELABEL(aps512xxn));
	profile.lifecycle_start_cycles = h1ProfileNow();
	profile.model_source_address = uint32_t(sourceAddress);
	profile.model_destination_address =
		uint32_t(reinterpret_cast<uintptr_t>(h1PsramStorage.model));
	profile.model_bytes = uint32_t(modelBytes);
	if (diagnostics) {
		printk("H1_MODEL_COPY_BEGIN stage=%s source=0x%08x destination=%p bytes=%u "
		       "psram_ready=%u\n",
		       stageName(stage), unsigned(sourceAddress), h1PsramStorage.model,
		       unsigned(modelBytes), unsigned(device_is_ready(ram)));
	}
	if (!device_is_ready(ram) ||
	    reinterpret_cast<uintptr_t>(h1PsramStorage.model) != H1_PSRAM_BASE ||
	    H1_PSRAM_BASE + sizeof(h1PsramStorage) > H1_PSRAM_END) {
		if (diagnostics) {
			printk("H1_FAIL_MODEL_CAPACITY_ALIGNMENT_OR_DEVICE stage=%s\n",
			       stageName(stage));
		}
		return nullptr;
	}

	const uint64_t sourceCrcStart = h1ProfileNow();
	const uint32_t sourceCrc = crc32(source, modelBytes);
	const uint64_t sourceCrcEnd = h1ProfileNow();
	profile.source_crc_cycles = sourceCrcEnd - sourceCrcStart;
	profile.source_crc_count = 1;
	profile.model_source_crc32 = sourceCrc;
	if (sourceCrc != expectedCrc || sourceCrcEnd <= sourceCrcStart) {
		if (diagnostics) {
			printk("H1_FAIL_MODEL_SOURCE_CRC stage=%s actual=%08x expected=%08x\n",
			       stageName(stage), sourceCrc, expectedCrc);
		}
		return nullptr;
	}

	for (unsigned index = 0; index < kGuardBytes; ++index) {
		h1PsramStorage.modelGuard[index] = guardPattern(index);
	}
	const uint64_t copyStart = h1ProfileNow();
	memcpy(h1PsramStorage.model, source, modelBytes);
	const uint64_t copyEnd = h1ProfileNow();
	profile.copy_cycles = copyEnd - copyStart;
	profile.copy_count = 1;

	const uint64_t destinationCrcStart = h1ProfileNow();
	const uint32_t destinationCrc = crc32(h1PsramStorage.model, modelBytes);
	const uint64_t destinationCrcEnd = h1ProfileNow();
	profile.destination_crc_cycles =
		destinationCrcEnd - destinationCrcStart;
	profile.destination_crc_count = 1;
	profile.model_destination_crc32 = destinationCrc;

	const uint64_t memcmpStart = h1ProfileNow();
	const int mismatch = memcmp(source, h1PsramStorage.model, modelBytes);
	const uint64_t memcmpEnd = h1ProfileNow();
	profile.memcmp_cycles = memcmpEnd - memcmpStart;
	profile.memcmp_count = 1;
	profile.model_memcmp_result = mismatch;
	if (diagnostics) {
		printk("H1_MODEL_COPY_DONE stage=%s source_crc=%08x destination_crc=%08x "
		       "memcmp=%d cycles=%llu\n",
		       stageName(stage), sourceCrc, destinationCrc, mismatch,
		       (unsigned long long)profile.copy_cycles);
	}

	const uint64_t validateStart = h1ProfileNow();
	if (destinationCrc != expectedCrc || mismatch != 0 || copyEnd <= copyStart ||
	    destinationCrcEnd <= destinationCrcStart || memcmpEnd <= memcmpStart ||
	    !h1ModelGuardCheck(stage, 0, 0, diagnostics)) {
		profile.validate_cycles += h1ProfileNow() - validateStart;
		if (diagnostics) {
			printk("H1_FAIL_MODEL_COPY_VERIFICATION stage=%s\n", stageName(stage));
		}
		return nullptr;
	}

	flatbuffers::Verifier verifier(h1PsramStorage.model, modelBytes);
	if (!tflite::VerifyModelBuffer(verifier)) {
		profile.validate_cycles += h1ProfileNow() - validateStart;
		if (diagnostics) {
			printk("H1_FAIL_MODEL_FLATBUFFER stage=%s\n", stageName(stage));
		}
		return nullptr;
	}
	const tflite::Model *model = tflite::GetModel(h1PsramStorage.model);
	if (model->version() != TFLITE_SCHEMA_VERSION) {
		profile.validate_cycles += h1ProfileNow() - validateStart;
		if (diagnostics) {
			printk("H1_FAIL_MODEL_SCHEMA stage=%s actual=%u expected=%u\n",
			       stageName(stage), unsigned(model->version()),
			       unsigned(TFLITE_SCHEMA_VERSION));
		}
		return nullptr;
	}
	for (unsigned index = 0; index < model->buffers()->size(); ++index) {
		const auto *buffer = model->buffers()->Get(index);
		if (buffer->offset() || buffer->size() ||
		    (buffer->data() && buffer->data()->size() &&
		     !withinModel(buffer->data()->data(), buffer->data()->size(), modelBytes))) {
			profile.validate_cycles += h1ProfileNow() - validateStart;
			if (diagnostics) {
				printk("H1_FAIL_MODEL_EXTERNAL_BUFFER stage=%s index=%u\n",
				       stageName(stage), index);
			}
			return nullptr;
		}
	}
	profile.validate_cycles += h1ProfileNow() - validateStart;
	if (diagnostics) {
		printk("H1_MODEL_READY stage=%s base=%p bytes=%u crc=%08x table=%p\n",
		       stageName(stage), h1PsramStorage.model, unsigned(modelBytes),
		       destinationCrc, model);
	}
	return model;
}

bool h1ModelGuardCheck(H1NpuStage stage, unsigned fixture, unsigned run,
		       bool diagnostics)
{
	for (unsigned index = 0; index < kGuardBytes; ++index) {
		if (h1PsramStorage.modelGuard[index] != guardPattern(index)) {
			if (diagnostics) {
				printk("H1_FAIL_MODEL_GUARD stage=%s fixture=%u run=%u offset=%u "
				       "expected=%02x actual=%02x\n",
				       stageName(stage), fixture, run, index, guardPattern(index),
				       h1PsramStorage.modelGuard[index]);
			}
			return false;
		}
	}
	if (diagnostics) {
		printk("H1_MODEL_GUARD_PASS stage=%s fixture=%u run=%u address=%p bytes=%u\n",
		       stageName(stage), fixture, run, h1PsramStorage.modelGuard,
		       unsigned(kGuardBytes));
	}
	return true;
}

H1Boundaries &h1CurrentBoundaries()
{
	return h1PsramStorage.current;
}

H1Boundaries &h1FirstBoundaries()
{
	return h1PsramStorage.first;
}

H1FrontendScratch &h1FrontendScratch()
{
	return h1PsramStorage.frontendScratch;
}

H1M55SpectralDiagnostics &h1M55SpectralDiagnostics()
{
	return h1PsramStorage.spectralDiagnostics;
}

H1M55SpectralWorkspace &h1M55SpectralWorkspace()
{
	return h1SpectralWorkspace;
}

H1M55CompactMel &h1M55CompactMel()
{
	return h1CompactMel;
}

uint8_t *h1UploadPayload()
{
	return h1PsramStorage.uploadPayload;
}

float *h1UploadedWaveform()
{
	return h1PsramStorage.uploadedWaveform;
}

int16_t *h1PcmRingStorage()
{
	return pcmRing;
}

int16_t *h1SelectedPcmWindow()
{
	return selectedPcmWindow;
}

char *h1ProtocolResponse()
{
	return protocolResponse;
}
