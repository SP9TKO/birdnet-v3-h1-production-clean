// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "build_identity.h"
#include "baseline_profile.h"
#include "lifecycle_reuse.h"
#include "topk_heap.hpp"
#include "validation_observer.hpp"
#if defined(H1_POSTPROCESSING_OBSERVATION)
#include "postprocessing_observer.hpp"
#endif
#if !defined(H1_DIAG_SKIP_PDM_INIT) || H1_DIAG_SKIP_PDM_INIT == 0
#include "audio_pdm.hpp"
#include "mic_usb.hpp"
#endif
#if defined(H1_I2S3_ACQUISITION)
#include "audio_i2s.hpp"
#endif
#include "frontend.hpp"
#if defined(H1_FRONTEND_DIAGNOSTICS)
#include "frontend_diagnostics.hpp"
#endif
#include "frontend_m55_spectral.hpp"
#include "h1_gem.hpp"
#include "classifier_bridge.hpp"
#include "h1_contract.h"
#include "model_storage.hpp"
#include "numeric_error_metrics.hpp"
#include "run_state.hpp"
#include "sha256.hpp"
#include "h1_memory_contract.h"
#include "h1_memory.hpp"
#include "runtime_profile.h"
#include "shared_feature.hpp"
#include "usb_transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#include <cmsis_core.h>
#include <ethosu_driver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

/* Optional transport and memory diagnostic commands. */
#define H1_FRAME_ACK_RETURN_DIAGNOSTIC 0
#define H1_RAW_MEMORY_READ_DIAGNOSTIC 0
#ifndef H1_DIAG_SKIP_PDM_INIT
#define H1_DIAG_SKIP_PDM_INIT 0
#endif

namespace {
using birdnet::h1::ExecutionMode;
using birdnet::h1::GemStatus;
using birdnet::h1::H1_GEM_CONSUMER;
using birdnet::h1::SharedFeatureLease;
using birdnet::h1::TensorDType;

constexpr size_t kTopCount = 100;
constexpr size_t kReportedTopCount = 10;
constexpr unsigned kFastGuardBytes = 64;

enum class FixtureId : unsigned {
	Synthetic = 1,
	Wren = 2,
	Uploaded = 3,
	Microphone = 4,
};

enum class RunStatus : uint32_t {
	NotRun = 0,
	Ok = 1,
	InputIdentity = 2,
	Frontend = 3,
	Backbone = 4,
	Gem = 5,
	Classifier = 6,
	Postprocess = 7,
	Profile = 8,
	NotReady = 9,
};

struct H1TopEntry {
	uint32_t index;
	uint32_t scoreBits;
};

struct H1RunResult {
	uint32_t resultVersion;
	uint32_t valid;
	uint32_t runSequence;
	FixtureId fixture;
	RunStatus status;
	uint32_t canonicalSynthetic;
	bool frontendNative;
	uint32_t inputCrc32;
	char inputSha256[65];
	uint32_t finiteCount;
	uint32_t thresholdCount;
	uint32_t scoreCrc32;
	uint32_t boundaryCrc32[7];
	uint32_t backboneSaturatedLow;
	uint32_t backboneSaturatedHigh;
	uint32_t classifierSaturatedLow;
	uint32_t classifierSaturatedHigh;
	uint32_t topCount;
	H1TopEntry top[kTopCount];
	uint32_t repeatComparable;
	uint32_t repeatEqual;
	H1RuntimeProfile profile;
};

struct H1UploadState {
	uint32_t valid;
	uint32_t frameCrcVerified;
	uint32_t metadataVerified;
	uint32_t rawCrcVerified;
	uint32_t finiteVerified;
	uint32_t canonicalByteMatch;
	uint32_t rawCrc32;
	uint32_t finiteCount;
	uint32_t uploadSequence;
	char declaredSha256[65];
};

H1RunResult gLastResult;
H1M55SpectralDiagnostics &gM55Spectral = h1M55SpectralDiagnostics();
H1M55FrontendRuntime gM55FrontendRuntime{};
uint32_t gM55ReflectedCrc;
uint32_t gM55WindowedCrc;
uint32_t gM55PowerCrc;
uint32_t gM55PowerDiagnosticCrc;
H1M55SpectralDiagnosticRecord gM55SpectralDiagnostic __noinit;
bool gM55SpectralValid;
H1M55SpectralTiming gM55LoopTiming;
uint64_t gM55LoopMinimumCycles;
uint64_t gM55LoopMaximumCycles;
uint32_t gM55LoopPowerMode;
bool gM55LoopValid;
uint32_t gM55LoopExitStep;
struct H1M55BoundedLoopResult {
	uint32_t valid;
	uint32_t requested;
	uint32_t completed;
	uint32_t status;
	uint32_t lastFrame;
	uint32_t lastStage;
	uint64_t totalCycles;
	uint32_t canariesOk;
};
H1M55BoundedLoopResult gM55BoundedLoop;
constexpr uint32_t kSpectralCanary = UINT32_C(0x51ec7a9d);
H1M55FrontendTiming gM55FrontendTiming;
H1FrontendStatus gM55FrontendStatus = H1FrontendStatus::InvalidInput;
H1M55FrontendExecution gM55FrontendExecution{};
bool gM55FrontendCommandCurrent;
bool gM55FrontendValid;
// True only while complete inference boundary buffers are current.
bool gInferenceDataValid;
#if H1_RAW_MEMORY_READ_DIAGNOSTIC
constexpr uint32_t kRawReadDoneMagic = UINT32_C(0x52444f4e);
constexpr uint32_t kRawReadUnarmed = UINT32_MAX;
volatile uint32_t gRawReadSramProbeWord = UINT32_C(0x5352414d);
volatile uint32_t gRawReadProbeId = kRawReadUnarmed;
volatile uint32_t gRawReadArmed;
volatile uint32_t gRawReadCompletionMarker;
#endif
H1UploadState gUpload;
uint32_t gRunSequence;
bool gComputeReady;
bool gModelStorageVerified;

enum class H1ContextState : uint32_t { Empty, Preparing, Ready, Failed };

struct alignas(32) H1PersistentStageContext {
	H1ContextState state;
	uint32_t preparationAttempts;
	uint32_t modelBindCount;
	uint32_t interpreterConstructionCount;
	uint32_t inputLookupCount;
	uint32_t outputLookupCount;
	uint32_t invocationCount;
	uint32_t pointerDriftCount;
	const tflite::Model *model;
	tflite::MicroMutableOpResolver<1> *resolver;
	tflite::MicroInterpreter *interpreter;
	uint8_t *arena;
	size_t arenaBytes;
	TfLiteTensor *inputTensor;
	TfLiteTensor *outputTensor;
	uint8_t *inputAddress;
	uint8_t *outputAddress;
	const tflite::Model *initialModelAddress;
	H1ModelLifecycleProfile initialization;
	alignas(tflite::MicroMutableOpResolver<1>)
	uint8_t resolverStorage[sizeof(tflite::MicroMutableOpResolver<1>)];
	alignas(tflite::MicroInterpreter)
	uint8_t interpreterStorage[sizeof(tflite::MicroInterpreter)];
};

/* No interpreter/resolver constructors run until explicit preparation. */
extern "C" {
H1PersistentStageContext h1PersistentStages[2];
uint32_t h1LifecycleInitializationComplete;
}

static_assert(H1_BACKBONE_ARENA_BYTES % 32 == 0);
static_assert(H1_CLASSIFIER_ARENA_ADDRESS % 32 == 0);
static_assert(H1_CLASSIFIER_ARENA_BYTES % 32 == 0);
static_assert(H1_ARENA_ADDRESS + H1_BACKBONE_ARENA_BYTES +
	      H1_LIFECYCLE_GUARD_BYTES == H1_CLASSIFIER_ARENA_ADDRESS);
static_assert(H1_CLASSIFIER_ARENA_ADDRESS + H1_CLASSIFIER_ARENA_BYTES +
	      H1_LIFECYCLE_GUARD_BYTES == H1_FAST_ADDRESS);

uint8_t *arenaGuard(unsigned stage)
{
	return h1TensorArena + (stage == 0 ? H1_BACKBONE_ARENA_BYTES :
		H1_ARENA_RESERVATION_BYTES - H1_LIFECYCLE_GUARD_BYTES);
}

uint8_t arenaGuardPattern(unsigned stage, unsigned index)
{
	return uint8_t(0x3du ^ (stage * 71u) ^ (index * 29u));
}

bool arenaGuardsCheck()
{
	for (unsigned stage = 0; stage < 2; ++stage) {
		const volatile uint8_t *guard = arenaGuard(stage);
		for (unsigned index = 0; index < H1_LIFECYCLE_GUARD_BYTES; ++index) {
			if (guard[index] != arenaGuardPattern(stage, index)) {
				return false;
			}
		}
	}
	return true;
}

bool withinArena(const H1PersistentStageContext &context, const void *pointer,
		 size_t bytes)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(context.arena);
	const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
	return address >= base && bytes <= context.arenaBytes &&
		address - base <= context.arenaBytes - bytes;
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

uint32_t readLe32(const uint8_t *source)
{
	return uint32_t(source[0]) | (uint32_t(source[1]) << 8) |
	       (uint32_t(source[2]) << 16) | (uint32_t(source[3]) << 24);
}

uint32_t floatBits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

float floatFromBits(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

const char *fixtureName(FixtureId fixture)
{
	switch (fixture) {
	case FixtureId::Synthetic:
		return "synthetic";
	case FixtureId::Wren:
		return "wren";
	case FixtureId::Uploaded:
		return "uploaded";
	case FixtureId::Microphone:
		return "microphone";
	}
	return "unknown";
}

const char *runStatusName(RunStatus status)
{
	switch (status) {
	case RunStatus::NotRun:
		return "not_run";
	case RunStatus::Ok:
		return "ok";
	case RunStatus::InputIdentity:
		return "input_identity";
	case RunStatus::Frontend:
		return "frontend";
	case RunStatus::Backbone:
		return "backbone";
	case RunStatus::Gem:
		return "gem";
	case RunStatus::Classifier:
		return "classifier";
	case RunStatus::Postprocess:
		return "postprocess";
	case RunStatus::Profile:
		return "profile";
	case RunStatus::NotReady:
		return "not_ready";
	}
	return "unknown";
}

bool append(char *buffer, size_t capacity, size_t &used, const char *format, ...)
{
	if (used >= capacity) {
		return false;
	}
	va_list arguments;
	va_start(arguments, format);
	const int written = vsnprintf(buffer + used, capacity - used, format, arguments);
	va_end(arguments);
	if (written < 0 || size_t(written) >= capacity - used) {
		return false;
	}
	used += size_t(written);
	return true;
}

void bytesToHex(const uint8_t *bytes, size_t count, char *output)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	for (size_t index = 0; index < count; ++index) {
		output[2 * index] = kDigits[bytes[index] >> 4];
		output[2 * index + 1] = kDigits[bytes[index] & 15];
	}
	output[2 * count] = '\0';
}

bool matches(const TfLiteTensor *tensor, const int *shape, int rank, size_t bytes,
	     uint32_t scaleBits)
{
	if (!tensor || tensor->type != kTfLiteInt16 || tensor->bytes != bytes ||
	    !tensor->dims || tensor->dims->size != rank ||
	    floatBits(tensor->params.scale) != scaleBits ||
	    tensor->params.zero_point != H1_QUANT_ZERO_POINT) {
		return false;
	}
	for (int dimension = 0; dimension < rank; ++dimension) {
		if (tensor->dims->data[dimension] != shape[dimension]) {
			return false;
		}
	}
	return true;
}

bool reportNpuIdentity()
{
	const device *npu = DEVICE_DT_GET(DT_NODELABEL(ethosu1));
	if (!device_is_ready(npu)) {
		printk("H1_FAIL_NPU_DEVICE_NOT_READY\n");
		return false;
	}
	struct ethosu_driver *driver = ethosu_reserve_driver();
	if (!driver) {
		printk("H1_FAIL_NPU_DRIVER_RESERVE\n");
		return false;
	}
	struct ethosu_driver_version driverVersion;
	struct ethosu_hw_info hardware;
	ethosu_get_driver_version(&driverVersion);
	ethosu_get_hw_info(driver, &hardware);
	printk("H1_NPU_ID driver=%u.%u.%u product=%u arch=%u.%u.%u version=%u.%u "
	       "macs_log2=%u macs=%u command_stream=%u custom_dma=%u fast=0x%08x "
	       "fast_bytes=%u\n",
	       driverVersion.major, driverVersion.minor, driverVersion.patch,
	       unsigned(hardware.version.product_major),
	       unsigned(hardware.version.arch_major_rev),
	       unsigned(hardware.version.arch_minor_rev),
	       unsigned(hardware.version.arch_patch_rev),
	       unsigned(hardware.version.version_major),
	       unsigned(hardware.version.version_minor), unsigned(hardware.cfg.macs_per_cc),
	       unsigned(1u << hardware.cfg.macs_per_cc),
	       unsigned(hardware.cfg.cmd_stream_version), unsigned(hardware.cfg.custom_dma),
	       unsigned(driver->fast_memory), unsigned(driver->fast_memory_size));
	const bool valid = driverVersion.major == 0 && driverVersion.minor == 16 &&
			   driverVersion.patch == 0 && hardware.version.product_major == 7 &&
			   hardware.cfg.macs_per_cc == 8 &&
			   driver->fast_memory == H1_FAST_ADDRESS &&
			   driver->fast_memory_size == H1_FAST_RESERVATION_BYTES;
	ethosu_release_driver(driver);
	if (!valid) {
		printk("H1_FAIL_NPU_IDENTITY\n");
	}
	return valid;
}

uint8_t fastGuardPattern(unsigned index)
{
	return static_cast<uint8_t>(0xa5u ^ (index * 37u));
}

bool fastGuardCheckQuiet()
{
	h1BaselineGuardInvalidate(H1_FAST_ADDRESS + H1_FAST_GUARD_OFFSET, kFastGuardBytes);
	SCB_InvalidateDCache_by_Addr(
		reinterpret_cast<uint32_t *>(h1FastMemory + H1_FAST_GUARD_OFFSET),
		kFastGuardBytes);
	h1BaselineGuardInvalidateDone();
	for (unsigned index = 0; index < kFastGuardBytes; ++index) {
		if (h1FastMemory[H1_FAST_GUARD_OFFSET + index] != fastGuardPattern(index)) {
			return false;
		}
	}
	return true;
}

bool preparePersistentStage(H1NpuStage stage)
{
	H1PersistentStageContext &context = h1PersistentStages[unsigned(stage)];
	if (context.state != H1ContextState::Empty) {
		return false;
	}
	context.state = H1ContextState::Preparing;
	++context.preparationAttempts;
	struct PreparationFailure {
		H1PersistentStageContext &context;
		~PreparationFailure() {
			if (context.state != H1ContextState::Ready) {
				context.state = H1ContextState::Failed;
			}
		}
	} failure{context};
	context.arena = stage == H1NpuStage::Backbone ? h1TensorArena :
		h1TensorArena + (H1_CLASSIFIER_ARENA_ADDRESS - H1_ARENA_ADDRESS);
	context.arenaBytes = stage == H1NpuStage::Backbone ?
		H1_BACKBONE_ARENA_BYTES : H1_CLASSIFIER_ARENA_BYTES;
	const size_t inputBytes = stage == H1NpuStage::Backbone ?
		H1_BACKBONE_INPUT_BYTES : H1_CLASSIFIER_INPUT_BYTES;
	const size_t outputBytes = stage == H1NpuStage::Backbone ?
		H1_SHARED_FEATURE_BYTES : H1_LOGIT_BYTES;
	H1ModelLifecycleProfile &lifecycle = context.initialization;
	const tflite::Model *model = h1PrepareNpuModel(stage, lifecycle, true);
	context.model = model;
	if (model) {
		++context.modelBindCount;
	}
	h1BaselineObserve(H1_EVENT_MODEL_CONTRACT_BEGIN, 0);
	const uint64_t modelContractStart = h1ProfileNow();
	const bool modelContract =
		model && model->operator_codes() && model->operator_codes()->size() == 1 &&
		model->subgraphs() && model->subgraphs()->size() == 1 &&
		model->subgraphs()->Get(0)->operators() &&
		model->subgraphs()->Get(0)->operators()->size() == 1 &&
		model->operator_codes()->Get(0)->builtin_code() ==
			tflite::BuiltinOperator_CUSTOM &&
		model->operator_codes()->Get(0)->custom_code() &&
		strcmp(model->operator_codes()->Get(0)->custom_code()->c_str(),
		       "ethos-u") == 0;
	const uint64_t modelContractEnd = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_MODEL_CONTRACT_END, 0);
	lifecycle.validate_cycles += modelContractEnd - modelContractStart;
	lifecycle.validate_count = 1;
	if (!modelContract || modelContractEnd <= modelContractStart) {
		return false;
	}

	const uint64_t runtimeInitStart = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_RESOLVER_SETUP_BEGIN, 0);
	context.resolver = new (context.resolverStorage) tflite::MicroMutableOpResolver<1>;
	tflite::MicroMutableOpResolver<1> &resolver = *context.resolver;
	if (resolver.AddEthosU() != kTfLiteOk) {
		return false;
	}
	h1BaselineObserve(H1_EVENT_RESOLVER_SETUP_END, 0);
	h1BaselineObserve(H1_EVENT_INTERPRETER_CONSTRUCT_BEGIN, 0);
	context.interpreter = new (context.interpreterStorage)
		tflite::MicroInterpreter(model, resolver, context.arena, context.arenaBytes);
	++context.interpreterConstructionCount;
	tflite::MicroInterpreter &interpreter = *context.interpreter;
	const uint64_t runtimeInitEnd = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_INTERPRETER_CONSTRUCT_END, 0);
	lifecycle.runtime_init_cycles = runtimeInitEnd - runtimeInitStart;
	lifecycle.runtime_init_count = 1;
	if (runtimeInitEnd <= runtimeInitStart) {
		return false;
	}

	h1BaselineObserve(H1_EVENT_ALLOCATE_TENSORS_BEGIN, 0);
	const uint64_t allocateStart = h1ProfileNow();
	const TfLiteStatus allocateStatus = interpreter.AllocateTensors();
	const uint64_t allocateEnd = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_ALLOCATE_TENSORS_END, 0);
	lifecycle.allocate_tensors_cycles = allocateEnd - allocateStart;
	lifecycle.allocate_tensors_count = 1;
	if (allocateStatus != kTfLiteOk || allocateEnd <= allocateStart) {
		return false;
	}

	const uint64_t tensorBindStart = h1ProfileNow();
	const int backboneInputShape[] = {1, 224, 281, 3};
	const int backboneOutputShape[] = {1, 7, 9, 1280};
	const int classifierInputShape[] = {1, 1280};
	const int classifierOutputShape[] = {1, 11560};
	h1BaselineObserve(H1_EVENT_INPUT_TENSOR_LOOKUP_BEGIN, 0);
	TfLiteTensor *inputTensor = interpreter.input(0);
	++context.inputLookupCount;
	h1BaselineObserve(H1_EVENT_INPUT_TENSOR_LOOKUP_END, 0);
	h1BaselineObserve(H1_EVENT_OUTPUT_TENSOR_LOOKUP_BEGIN, 0);
	TfLiteTensor *outputTensor = interpreter.output(0);
	++context.outputLookupCount;
	h1BaselineObserve(H1_EVENT_OUTPUT_TENSOR_LOOKUP_END, 0);
	const size_t arenaUsed = interpreter.arena_used_bytes();
	const bool tensorContract = stage == H1NpuStage::Backbone
		? matches(inputTensor, backboneInputShape, 4, H1_BACKBONE_INPUT_BYTES,
			  H1_BACKBONE_INPUT_SCALE_BITS) &&
		  matches(outputTensor, backboneOutputShape, 4,
			  H1_SHARED_FEATURE_BYTES, H1_SHARED_FEATURE_SCALE_BITS)
		: matches(inputTensor, classifierInputShape, 2,
			  H1_CLASSIFIER_INPUT_BYTES, H1_CLASSIFIER_INPUT_SCALE_BITS) &&
		  matches(outputTensor, classifierOutputShape, 2, H1_LOGIT_BYTES,
			  H1_LOGIT_SCALE_BITS);
	const bool tensorBindingsValid =
		interpreter.inputs_size() == 1 && interpreter.outputs_size() == 1 &&
		tensorContract && inputTensor->bytes == inputBytes &&
		outputTensor->bytes == outputBytes &&
		arenaUsed <= context.arenaBytes &&
		withinArena(context, inputTensor->data.uint8, inputBytes) &&
		withinArena(context, outputTensor->data.uint8, outputBytes);
	const uint64_t tensorBindEnd = h1ProfileNow();
	lifecycle.tensor_bind_cycles = tensorBindEnd - tensorBindStart;
	lifecycle.tensor_bind_count = 1;
	lifecycle.arena_used_bytes = uint32_t(arenaUsed);
	lifecycle.input_bytes = uint32_t(inputBytes);
	lifecycle.output_bytes = uint32_t(outputBytes);
	if (!tensorBindingsValid || tensorBindEnd <= tensorBindStart) {
		return false;
	}

	context.inputTensor = inputTensor;
	context.outputTensor = outputTensor;
	context.inputAddress = inputTensor->data.uint8;
	context.outputAddress = outputTensor->data.uint8;
	context.initialModelAddress = model;
	if (!arenaGuardsCheck() || !h1ModelGuardCheck(stage, 0, 0, true)) {
		return false;
	}
	lifecycle.lifecycle_end_cycles = h1ProfileNow();
	lifecycle.lifecycle_cycles =
		lifecycle.lifecycle_end_cycles - lifecycle.lifecycle_start_cycles;
	context.state = H1ContextState::Ready;
	h1BaselineTensorBind(unsigned(stage), uint32_t(uintptr_t(context.inputAddress)),
		uint32_t(inputBytes), uint32_t(uintptr_t(context.outputAddress)),
		uint32_t(outputBytes), uint32_t(arenaUsed));
	printk("H1_CONTEXT_READY stage=%u context=%p model=%p interpreter=%p "
	       "arena=%p arena_bytes=%u arena_used=%u input=%p output=%p "
	       "copies=%u constructions=%u allocations=%u binds=%u\n",
	       unsigned(stage), &context, model, context.interpreter, context.arena,
	       unsigned(context.arenaBytes), unsigned(arenaUsed), context.inputAddress,
	       context.outputAddress, lifecycle.copy_count,
	       context.interpreterConstructionCount, lifecycle.allocate_tensors_count,
	       context.modelBindCount);
	return true;
}

bool preparePersistentContexts()
{
	for (unsigned stage = 0; stage < 2; ++stage) {
		uint8_t *guard = arenaGuard(stage);
		for (unsigned index = 0; index < H1_LIFECYCLE_GUARD_BYTES; ++index) {
			guard[index] = arenaGuardPattern(stage, index);
		}
		SCB_CleanInvalidateDCache_by_Addr(
			reinterpret_cast<uint32_t *>(guard), H1_LIFECYCLE_GUARD_BYTES);
	}
	if (!preparePersistentStage(H1NpuStage::Backbone) ||
	    !preparePersistentStage(H1NpuStage::Classifier)) {
		return false;
	}
	h1LifecycleInitializationComplete = 1;
	return h1LifecycleReuseReady();
}

bool invokeNpu(H1NpuStage stage, const void *inputData, size_t inputBytes,
	       void *outputData, size_t outputBytes, FixtureId fixture, unsigned run,
	       H1NpuProfile &profile)
{
	H1ModelLifecycleProfile &lifecycle = profile.lifecycle;
	h1BaselineStageBegin(stage == H1NpuStage::Backbone ? 0u : 1u);
	h1RunStateMark(stage == H1NpuStage::Backbone
		? H1RunState::BackbonePrepareBegin
		: H1RunState::ClassifierPrepareBegin);
	lifecycle.lifecycle_start_cycles = h1ProfileNow();
	H1PersistentStageContext &context = h1PersistentStages[unsigned(stage)];
	const uintptr_t expectedArena = stage == H1NpuStage::Backbone ?
		H1_ARENA_ADDRESS : H1_CLASSIFIER_ARENA_ADDRESS;
	if (!h1LifecycleReuseReady() ||
	    context.model != context.initialModelAddress ||
	    context.resolver != reinterpret_cast<void *>(context.resolverStorage) ||
	    context.interpreter != reinterpret_cast<void *>(context.interpreterStorage) ||
	    uintptr_t(context.arena) != expectedArena ||
	    context.inputTensor->data.uint8 != context.inputAddress ||
	    context.outputTensor->data.uint8 != context.outputAddress ||
	    context.inputTensor->bytes != inputBytes ||
	    context.outputTensor->bytes != outputBytes || !arenaGuardsCheck()) {
		++context.pointerDriftCount;
		context.state = H1ContextState::Failed;
		return false;
	}
	++context.invocationCount;
	const H1ModelLifecycleProfile &initial = context.initialization;
	lifecycle.model_source_address = initial.model_source_address;
	lifecycle.model_destination_address = initial.model_destination_address;
	lifecycle.model_bytes = initial.model_bytes;
	lifecycle.model_source_crc32 = initial.model_source_crc32;
	lifecycle.model_destination_crc32 = initial.model_destination_crc32;
	lifecycle.model_memcmp_result = initial.model_memcmp_result;
	lifecycle.arena_used_bytes = initial.arena_used_bytes;
	lifecycle.input_bytes = uint32_t(inputBytes);
	lifecycle.output_bytes = uint32_t(outputBytes);
	const size_t arenaUsed = initial.arena_used_bytes;
	TfLiteTensor *inputTensor = context.inputTensor;
	TfLiteTensor *outputTensor = context.outputTensor;
	tflite::MicroInterpreter &interpreter = *context.interpreter;

	h1BaselineTensorBind(stage == H1NpuStage::Backbone ? 0u : 1u,
		uint32_t(reinterpret_cast<uintptr_t>(inputTensor->data.uint8)), uint32_t(inputBytes),
		uint32_t(reinterpret_cast<uintptr_t>(outputTensor->data.uint8)), uint32_t(outputBytes),
		uint32_t(arenaUsed));
	h1BaselineObserve(H1_EVENT_INPUT_COPY_BEGIN, 0);
	const uint64_t inputCopyStart = h1ProfileNow();
	memcpy(inputTensor->data.uint8, inputData, inputBytes);
	const uint64_t inputCopyEnd = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_INPUT_COPY_END, 0);
	lifecycle.input_copy_cycles = inputCopyEnd - inputCopyStart;
	lifecycle.input_copy_count = 1;
	if (inputCopyEnd <= inputCopyStart) {
		return false;
	}

	h1RunStateMark(stage == H1NpuStage::Backbone
		? H1RunState::BackbonePrepareDone
		: H1RunState::ClassifierPrepareDone);
	const uint32_t irqBefore = h1IrqCount;
	h1ProfileInvokeBegin(&profile);
	lifecycle.lifecycle_end_cycles = profile.invoke_start_cycles;
	h1RunStateMark(stage == H1NpuStage::Backbone
		? H1RunState::BackboneInvokeBegin
		: H1RunState::ClassifierInvokeBegin);
	h1BaselineObserve(H1_EVENT_NPU_INVOKE_BEGIN, 0);
	const TfLiteStatus status = interpreter.Invoke();
	h1BaselineObserve(H1_EVENT_NPU_INVOKE_END, 0);
	h1ProfileInvokeEnd(&profile, int32_t(status));
	h1RunStateMark(stage == H1NpuStage::Backbone
		? H1RunState::BackboneInvokeDone
		: H1RunState::ClassifierInvokeDone);
	const uint32_t irqAfter = h1IrqCount;
	if (status != kTfLiteOk || irqAfter - irqBefore != 1 ||
	    !fastGuardCheckQuiet() ||
	    !h1ModelGuardCheck(stage, unsigned(fixture), run, false) ||
	    !arenaGuardsCheck()) {
		return false;
	}

	h1BaselineObserve(H1_EVENT_OUTPUT_COPY_BEGIN, 0);
	const uint64_t outputCopyStart = h1ProfileNow();
	memcpy(outputData, outputTensor->data.uint8, outputBytes);
	const uint64_t outputCopyEnd = h1ProfileNow();
	h1BaselineObserve(H1_EVENT_OUTPUT_COPY_END, 0);
	lifecycle.output_copy_cycles = outputCopyEnd - outputCopyStart;
	lifecycle.output_copy_count = 1;
	h1BaselineObserve(H1_EVENT_LIFECYCLE_STAGE_END, 0);
	return outputCopyEnd > outputCopyStart;
}

void quantizeBackboneInput(const float *input, int16_t *output, size_t elements,
			   size_t &saturatedLow, size_t &saturatedHigh)
{
	const float scale = floatFromBits(H1_BACKBONE_INPUT_SCALE_BITS);
	saturatedLow = 0;
	saturatedHigh = 0;
	for (size_t index = 0; index < elements; ++index) {
		const double scaled = static_cast<double>(input[index]) /
				      static_cast<double>(scale);
		double rounded = std::nearbyint(scaled);
		if (rounded < -32768.0) {
			rounded = -32768.0;
			++saturatedLow;
		} else if (rounded > 32767.0) {
			rounded = 32767.0;
			++saturatedHigh;
		}
		output[index] = static_cast<int16_t>(rounded);
	}
}


bool betterScore(const float *scores, uint32_t left, uint32_t right)
{
	return scores[left] > scores[right] ||
	       (scores[left] == scores[right] && left < right);
}

bool postprocess(H1Boundaries &boundaries, H1RunResult &result)
{
	const float scale = floatFromBits(H1_LOGIT_SCALE_BITS);
	const float threshold = floatFromBits(H1_REPORT_THRESHOLD_BITS);
#if defined(H1_POSTPROCESSING_OBSERVATION)
	h1PostprocessObserverPrepare();
	auto &observation = h1PostprocessObserverState.last;
	observation.scoreStart = h1ProfileNow();
#endif
	for (size_t index = 0; index < H1_LOGIT_ELEMENTS; ++index) {
		const float logit = static_cast<float>(boundaries.logits[index]) * scale;
		const float score = 1.0f / (1.0f + ::expf(-logit));
		boundaries.scores[index] = score;
		result.finiteCount += std::isfinite(score);
		result.thresholdCount += score >= threshold;
	}
#if defined(H1_POSTPROCESSING_OBSERVATION)
	observation.scoreEnd = h1ProfileNow();
#endif
	if (result.finiteCount != H1_LOGIT_ELEMENTS) {
		return false;
	}
#if defined(H1_POSTPROCESSING_OBSERVATION)
	H1TopkHeapCounts heapCounts{};
	observation.topStart = h1ProfileNow();
#endif
	uint32_t top[kTopCount];
#if defined(H1_POSTPROCESSING_OBSERVATION)
	const size_t count = h1BaselineState.mode == H1_BASELINE_ACCEPTANCE
		? h1SelectTopkHeap<false>(boundaries.scores, H1_LOGIT_ELEMENTS,
					 top, kTopCount, betterScore, heapCounts)
		: h1SelectTopkHeap<true>(boundaries.scores, H1_LOGIT_ELEMENTS,
					top, kTopCount, betterScore, heapCounts);
#else
	H1TopkHeapCounts heapCounts{};
	const size_t count = h1SelectTopkHeap<false>(boundaries.scores, H1_LOGIT_ELEMENTS,
						  top, kTopCount, betterScore, heapCounts);
#endif
#if defined(H1_POSTPROCESSING_OBSERVATION)
	observation.topEnd = h1ProfileNow();
#endif
	result.topCount = uint32_t(count);
	for (size_t rank = 0; rank < count; ++rank) {
		result.top[rank].index = top[rank];
		result.top[rank].scoreBits = floatBits(boundaries.scores[top[rank]]);
	}
#if defined(H1_POSTPROCESSING_OBSERVATION)
	observation.materialEnd = h1ProfileNow();
	observation.heap = heapCounts;
	observation.comparisons = heapCounts.betterScoreCalls;
	observation.shifts = 0;
	observation.selected = uint32_t(count);
#endif
	return true;
}

void recordBoundaryCrcs(H1RunResult &result, const H1Boundaries &boundaries)
{
	H1_VALIDATION_MARK(H1V_BOUNDARY_0_BEGIN);
	result.boundaryCrc32[0] = crc32(reinterpret_cast<const uint8_t *>(boundaries.frontend),
					H1_FRONTEND_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_0_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_1_BEGIN);
	result.boundaryCrc32[1] = crc32(
		reinterpret_cast<const uint8_t *>(boundaries.backboneInput),
		H1_BACKBONE_INPUT_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_1_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_2_BEGIN);
	result.boundaryCrc32[2] = crc32(
		reinterpret_cast<const uint8_t *>(boundaries.sharedFeature),
		H1_SHARED_FEATURE_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_2_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_3_BEGIN);
	result.boundaryCrc32[3] = crc32(reinterpret_cast<const uint8_t *>(boundaries.embedding),
					H1_EMBEDDING_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_3_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_4_BEGIN);
	result.boundaryCrc32[4] = crc32(
		reinterpret_cast<const uint8_t *>(boundaries.classifierInput),
		H1_CLASSIFIER_INPUT_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_4_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_5_BEGIN);
	result.boundaryCrc32[5] = crc32(reinterpret_cast<const uint8_t *>(boundaries.logits),
					H1_LOGIT_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_5_END);
	H1_VALIDATION_MARK(H1V_BOUNDARY_6_BEGIN);
	result.boundaryCrc32[6] = crc32(reinterpret_cast<const uint8_t *>(boundaries.scores),
					H1_SCORE_BYTES);
	H1_VALIDATION_MARK(H1V_BOUNDARY_6_END);
	result.scoreCrc32 = result.boundaryCrc32[6];
	H1_VALIDATION_MARK(H1V_SCORE_ALIAS_END);
}

bool runOnce(FixtureId fixture, const float *waveform, uint32_t expectedInputCrc,
	     const char *inputSha256, bool canonicalSynthetic, H1RunResult &result,
	     bool legacyReference = false)
{
	H1_VALIDATION_MARK(H1V_RUN_ENTER);
	gInferenceDataValid = false;
	gM55FrontendValid = false;
	h1RunStateMark(H1RunState::RunOnceEnter);
	memset(&result, 0, sizeof(result));
	result.resultVersion = 1;
	result.runSequence = ++gRunSequence;
	result.fixture = fixture;
	result.status = RunStatus::NotRun;
	result.canonicalSynthetic = canonicalSynthetic;
	result.frontendNative = !legacyReference;
	if (!gComputeReady || !gModelStorageVerified) {
		result.status = RunStatus::NotReady;
		return false;
	}
	H1_VALIDATION_MARK(H1V_WAVEFORM_CRC_BEGIN);
	result.inputCrc32 = crc32(reinterpret_cast<const uint8_t *>(waveform),
				  H1_WAVEFORM_BYTES);
	H1_VALIDATION_MARK(H1V_WAVEFORM_CRC_END);
	strncpy(result.inputSha256, inputSha256, sizeof(result.inputSha256) - 1);
	H1_VALIDATION_MARK(H1V_IDENTITY_COPY_END);
	if (result.inputCrc32 != expectedInputCrc) {
		result.status = RunStatus::InputIdentity;
		return false;
	}

	H1_VALIDATION_MARK(H1V_IDENTITY_CHECK_END);
	H1Boundaries &boundaries = h1CurrentBoundaries();
	H1FrontendScratch &scratch = h1FrontendScratch();
	h1ProfileReset(&result.profile);
	const uint64_t totalStart = h1ProfileNow();

	const uint64_t frontendStart = h1ProfileNow();
	h1BaselineMark(0u, frontendStart);
	h1RunStateMark(H1RunState::FrontendBegin);
	result.profile.pre_frontend_overhead_cycles =
		frontendStart - totalStart;
	H1_VALIDATION_MARK(H1V_FRONTEND_WORK_BEGIN);
	const H1FrontendStatus frontendStatus = legacyReference
		? h1RunFrontendLegacy(waveform, H1_WAVEFORM_ELEMENTS, scratch,
			boundaries.frontend, H1_FRONTEND_ELEMENTS)
		: h1RunFrontend(waveform, H1_WAVEFORM_ELEMENTS, scratch,
			gM55FrontendRuntime, boundaries.frontend, H1_FRONTEND_ELEMENTS);
	const uint64_t frontendEnd = h1ProfileNow();
	h1BaselineMark(1u, frontendEnd);
	result.profile.frontend_cycles = frontendEnd - frontendStart;
	if (frontendStatus != H1FrontendStatus::Ok || frontendEnd <= frontendStart) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Frontend;
		return false;
	}
	h1RunStateMark(H1RunState::FrontendDone);

	size_t saturatedLow = 0;
	size_t saturatedHigh = 0;
	const uint64_t backboneQuantizeStart = h1ProfileNow();
	h1BaselineMark(2u, backboneQuantizeStart);
	quantizeBackboneInput(boundaries.frontend, boundaries.backboneInput,
			      H1_BACKBONE_INPUT_ELEMENTS, saturatedLow, saturatedHigh);
	const uint64_t backboneQuantizeEnd = h1ProfileNow();
	h1BaselineMark(3u, backboneQuantizeEnd);
	result.profile.frontend_to_backbone_quantize_cycles =
		backboneQuantizeEnd - backboneQuantizeStart;
	result.backboneSaturatedLow = uint32_t(saturatedLow);
	result.backboneSaturatedHigh = uint32_t(saturatedHigh);
	if (!invokeNpu(H1NpuStage::Backbone, boundaries.backboneInput,
		       H1_BACKBONE_INPUT_BYTES, boundaries.sharedFeature,
		       H1_SHARED_FEATURE_BYTES, fixture, result.runSequence,
		       result.profile.backbone)) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Backbone;
		return false;
	}
	result.profile.frontend_to_backbone_invoke_cycles =
		result.profile.backbone.invoke_start_cycles - frontendEnd;

	SharedFeatureLease lease{
		{boundaries.sharedFeature,
		 {{1, 7, 9, 1280}},
		 TensorDType::Int16,
		 floatFromBits(H1_SHARED_FEATURE_SCALE_BITS),
		 H1_QUANT_ZERO_POINT,
		 H1_SHARED_FEATURE_BYTES},
		ExecutionMode::H1_ONLY,
		0,
	};
	const uint64_t gemStart = h1ProfileNow();
	h1BaselineMark(4u, gemStart);
	h1RunStateMark(H1RunState::GemBegin);
	result.profile.backbone_to_gem_handoff_cycles =
		gemStart - result.profile.backbone.invoke_end_cycles;
	const GemStatus gemStatus = birdnet::h1::runGem(
		lease.feature, boundaries.embedding, H1_EMBEDDING_ELEMENTS);
	const uint64_t gemEnd = h1ProfileNow();
	h1BaselineMark(5u, gemEnd);
	result.profile.gem_cycles = gemEnd - gemStart;
	if (gemStatus != GemStatus::Ok ||
	    !birdnet::h1::completeConsumer(lease, H1_GEM_CONSUMER) ||
	    !birdnet::h1::mayRelease(lease)) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Gem;
		return false;
	}
	h1RunStateMark(H1RunState::GemDone);

	const uint64_t classifierQuantizeStart = h1ProfileNow();
	h1BaselineMark(6u, classifierQuantizeStart);
	if (!birdnet::h1::quantizeClassifierInput(
		    boundaries.embedding, boundaries.classifierInput,
		    H1_CLASSIFIER_INPUT_ELEMENTS, saturatedLow, saturatedHigh)) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Gem;
		return false;
	}
	const uint64_t classifierQuantizeEnd = h1ProfileNow();
	h1BaselineMark(7u, classifierQuantizeEnd);
	result.profile.embedding_to_classifier_quantize_cycles =
		classifierQuantizeEnd - classifierQuantizeStart;
	result.classifierSaturatedLow = uint32_t(saturatedLow);
	result.classifierSaturatedHigh = uint32_t(saturatedHigh);
	if (!invokeNpu(H1NpuStage::Classifier, boundaries.classifierInput,
		       H1_CLASSIFIER_INPUT_BYTES, boundaries.logits, H1_LOGIT_BYTES,
		       fixture, result.runSequence, result.profile.classifier)) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Classifier;
		return false;
	}
	result.profile.gem_to_classifier_handoff_cycles =
		result.profile.classifier.invoke_start_cycles - gemEnd;

	const uint64_t postprocessStart = h1ProfileNow();
	h1RunStateMark(H1RunState::PostprocessBegin);
	result.profile.classifier_to_postprocess_handoff_cycles =
		postprocessStart - result.profile.classifier.invoke_end_cycles;
	if (!postprocess(boundaries, result)) {
		result.profile.total_compute_cycles = h1ProfileNow() - totalStart;
		result.status = RunStatus::Postprocess;
		return false;
	}
	h1RunStateMark(H1RunState::PostprocessDone);
	const uint64_t postprocessEnd = h1ProfileNow();
	H1_VALIDATION_AT(H1V_P0_END, postprocessEnd);
	result.profile.postprocess_cycles = postprocessEnd - postprocessStart;
	result.profile.total_compute_cycles = postprocessEnd - totalStart;
#if defined(H1_POSTPROCESSING_OBSERVATION)
	h1PostprocessObserverFinalize(postprocessStart, postprocessEnd,
				      result.runSequence, result.profile.clock_hz);
#endif

	/* Evidence/checkpoint CRC serialization is intentionally outside timing. */
	if (!h1ProfileFinalize(&result.profile)) {
		result.status = RunStatus::Profile;
		return false;
	}
	H1_VALIDATION_MARK(H1V_PROFILE_FINALIZE_END);
	recordBoundaryCrcs(result, boundaries);
	H1_VALIDATION_MARK(H1V_RESULT_STATE_BEGIN);
	result.status = RunStatus::Ok;
	result.valid = 1;
	H1_VALIDATION_MARK(H1V_RESULT_STATE_END);
	gInferenceDataValid = true;
	H1_VALIDATION_MARK(H1V_PUBLICATION_END);
	return true;
}

bool sendJson(const H1UsbFrame &request, const char *json, size_t length);
void sendError(const H1UsbFrame &request, const char *code, const char *detail);

uint32_t spectralBits(unsigned kind, uint32_t offset)
{
	float value = 0.0f;
	switch (kind) {
	case 0: value = gM55Spectral.reflected[offset]; break;
	case 1: value = gM55Spectral.windowed[offset]; break;
	case 2: {
		const unsigned bin = offset;
		const unsigned index = bin == 0 ? 0 : (bin == 1024 ? 1 : 2 * bin);
		value = h1M55SpectralWorkspace().samples[index];
		break;
	}
	case 3: {
		const unsigned bin = offset;
		const unsigned index = bin == 0 ? 0 : (bin == 1024 ? 1 : 2 * bin + 1);
		value = (bin == 0 || bin == 1024) ? 0.0f : h1M55SpectralWorkspace().samples[index];
		break;
	}
	case 4: value = gM55Spectral.power[offset]; break;
	case 5: value = gM55Spectral.powerDiagnostic[offset]; break;
	default: break;
	}
	return floatBits(value);
}


void runM55SpectralFrame(const H1UsbFrame &request)
{
	gM55FrontendCommandCurrent = false;
	gM55BoundedLoop.valid = 0;
	gM55LoopValid = false;
	gM55FrontendValid = false;
	const uint32_t frame = readLe32(request.payload);
	const uint32_t stopAfter = request.payloadLength >= 8
		? readLe32(request.payload + 4) : H1_M55_STAGE_POWER_SQUARES;
	if (frame >= H1_M55_FRAME_COUNT) {
		sendError(request, "FRAME_OUT_OF_RANGE", "frame must be in the range 0..187");
		return;
	}
	if (stopAfter < H1_M55_STAGE_INIT ||
	    stopAfter > H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED) {
		sendError(request, "INVALID_STOP_STAGE", "stop_after must be stage 1..8");
		return;
	}
	static constexpr char kPreExecutionAck[] = "{\"ok\":true,\"accepted\":true}";
	if (!sendJson(request, kPreExecutionAck, sizeof(kPreExecutionAck) - 1)) {
		return;
	}

#if H1_FRAME_ACK_RETURN_DIAGNOSTIC
	return;
#else
	/* Execution boundary: spectral PSRAM and diagnostic state begin below. */
	h1M55SpectralDiagnosticBegin(&gM55SpectralDiagnostic, request.sequence,
				     frame, stopAfter);
	gM55Spectral.timing = H1M55SpectralTiming{};
	gM55Spectral.timing.clockHz = sys_clock_hw_cycles_per_sec();
	gM55SpectralValid = false;

	// Every diagnostic invocation reconstructs both CMSIS instances from a
	// fresh context, including commands that stop at the INIT boundary.
	h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_INIT, false);
	if (!h1M55SpectralPrepare(&gM55Spectral.context, &h1M55SpectralWorkspace())) {
		return;
	}
	h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_INIT, true);
	if (stopAfter == H1_M55_STAGE_INIT) {
		return;
	}
	if (stopAfter >= H1_M55_STAGE_CFFT) {
		h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic,
					     H1_M55_STAGE_CFFT, false);
		h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic,
					     H1_M55_STAGE_CFFT, true);
	}

	const float *hann = nullptr;
	if (stopAfter >= H1_M55_STAGE_HANN && !h1GetFrontendHann(&hann)) {
		return;
	}
	H1M55SpectralCapture capture{
		gM55Spectral.reflected,
		gM55Spectral.windowed,
		gM55Spectral.power,
		gM55Spectral.powerDiagnostic,
		&gM55Spectral.timing,
		&gM55SpectralDiagnostic
	};
	const float *const waveform = h1UploadedWaveform();
	if (!h1M55SpectralProcessFrame(&gM55Spectral.context, waveform,
				       H1_WAVEFORM_ELEMENTS, hann, int(frame), &capture,
				       stopAfter, stopAfter != H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED)) {
		return;
	}

	if (stopAfter < H1_M55_STAGE_POWER_SQUARES) return;

	gM55ReflectedCrc = crc32(reinterpret_cast<const uint8_t *>(gM55Spectral.reflected),
				 sizeof(gM55Spectral.reflected));
	gM55WindowedCrc = crc32(reinterpret_cast<const uint8_t *>(gM55Spectral.windowed),
				 sizeof(gM55Spectral.windowed));
	gM55PowerCrc = crc32(reinterpret_cast<const uint8_t *>(gM55Spectral.power),
			      sizeof(gM55Spectral.power));
	gM55PowerDiagnosticCrc = crc32(
		reinterpret_cast<const uint8_t *>(gM55Spectral.powerDiagnostic),
		sizeof(gM55Spectral.powerDiagnostic));
	gM55SpectralValid = true;
#endif
}

void runM55SpectralLoop(const H1UsbFrame &request)
{
	gM55FrontendCommandCurrent = false;
	gM55BoundedLoop.valid = 0;
	const uint32_t powerMode = request.payloadLength >= 4 ? readLe32(request.payload) : 0;
	if (powerMode != H1_M55_STAGE_POWER_SQUARES &&
	    powerMode != H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED) {
		sendError(request, "INVALID_POWER_MODE", "select stage 7 scalar or stage 8 CMSIS power");
		return;
	}
	static constexpr char kAck[] = "{\"ok\":true,\"accepted\":true}";
	if (!sendJson(request, kAck, sizeof(kAck) - 1)) return;

	const float *const waveform = h1UploadedWaveform();
	const float *hann = nullptr;
	gM55LoopExitStep = 0;
	if (!h1M55SpectralPrepare(&gM55Spectral.context, &h1M55SpectralWorkspace())) {
		gM55LoopExitStep = 2;
		return;
	}
	if (!h1GetFrontendHann(&hann)) {
		gM55LoopExitStep = 3;
		return;
	}

	gM55LoopTiming = H1M55SpectralTiming{};
	gM55LoopTiming.clockHz = sys_clock_hw_cycles_per_sec();
	gM55LoopMinimumCycles = UINT64_MAX;
	gM55LoopMaximumCycles = 0;
	gM55LoopPowerMode = powerMode;
	gM55LoopValid = false;
	const uint32_t selectedStage = powerMode;
	const bool computeScalar = powerMode == H1_M55_STAGE_POWER_SQUARES;
	const uint64_t loopStart = k_cycle_get_64();
	for (int frame = 0; frame < H1_M55_FRAME_COUNT; ++frame) {
		H1M55SpectralTiming frameTiming{};
		H1M55SpectralCapture capture{nullptr, nullptr, nullptr, gM55Spectral.power,
					     &frameTiming, nullptr};
		if (!h1M55SpectralProcessFrame(&gM55Spectral.context, waveform,
					       H1_WAVEFORM_ELEMENTS, hann, frame, &capture,
					       selectedStage, computeScalar)) {
			gM55LoopExitStep = 4;
			return;
		}
		gM55LoopTiming.framePreparationCycles += frameTiming.framePreparationCycles;
		gM55LoopTiming.hannCycles += frameTiming.hannCycles;
		gM55LoopTiming.cfftCycles += frameTiming.cfftCycles;
		gM55LoopTiming.realSplitCycles += frameTiming.realSplitCycles;
		gM55LoopTiming.powerSquaresCycles += frameTiming.powerSquaresCycles;
		gM55LoopTiming.powerCmsisMagSquaredCycles += frameTiming.powerCmsisMagSquaredCycles;
		gM55LoopTiming.totalCycles += frameTiming.totalCycles;
		const uint64_t frameCycles = frameTiming.totalCycles;
		if (frameCycles < gM55LoopMinimumCycles) gM55LoopMinimumCycles = frameCycles;
		if (frameCycles > gM55LoopMaximumCycles) gM55LoopMaximumCycles = frameCycles;
	}
	gM55LoopTiming.totalCycles = k_cycle_get_64() - loopStart;
	gM55Spectral.timing = gM55LoopTiming;
	gM55LoopExitStep = 5;
	gM55LoopValid = true;
}

void runM55SpectralBoundedLoop(const H1UsbFrame &request)
{
	gM55FrontendCommandCurrent = false;
	const uint32_t requested = readLe32(request.payload);
	if (requested == 0 || requested > H1_M55_FRAME_COUNT) {
		sendError(request, "INVALID_FRAME_COUNT", "frames must be in the range 1..188");
		return;
	}
	static constexpr char kAck[] = "{\"ok\":true,\"accepted\":true}";
	if (!sendJson(request, kAck, sizeof(kAck) - 1)) return;

	gM55BoundedLoop = H1M55BoundedLoopResult{};
	gM55BoundedLoop.valid = 1;
	gM55BoundedLoop.requested = requested;
	gM55BoundedLoop.lastFrame = UINT32_MAX;
	gM55BoundedLoop.canariesOk = 1;
	gM55LoopValid = false;
	gM55FrontendValid = false;
	H1M55SpectralWorkspace &workspace = h1M55SpectralWorkspace();
	gM55Spectral.powerCanaryBefore = kSpectralCanary;
	gM55Spectral.powerCanaryAfter = ~kSpectralCanary;
	h1M55SpectralDiagnosticBegin(&gM55SpectralDiagnostic, request.sequence, 0,
				     H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED);
	h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_INIT, false);
	const float *const waveform = h1UploadedWaveform();
	const uint64_t start = k_cycle_get_64();
	const bool fftReady = h1M55SpectralPrepare(&gM55Spectral.context, &workspace);
	if (fftReady) {
		h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_INIT, true);
		h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_CFFT, true);
	}
	h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_HANN, false);
	const float *hann = nullptr;
	const bool hannReady = fftReady && h1GetFrontendHann(&hann);
	if (hannReady) h1M55SpectralDiagnosticMark(&gM55SpectralDiagnostic, H1_M55_STAGE_HANN, true);
	if (!hannReady) {
		gM55BoundedLoop.status = 1;
		gM55BoundedLoop.totalCycles = k_cycle_get_64() - start;
		return;
	}

	for (uint32_t frame = 0; frame < requested; ++frame) {
		gM55BoundedLoop.lastFrame = frame;
		h1M55SpectralDiagnosticBegin(&gM55SpectralDiagnostic, request.sequence,
					     frame, H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED);
		H1M55SpectralTiming frameTiming{};
		H1M55SpectralCapture capture{nullptr, nullptr, nullptr, gM55Spectral.power,
					     &frameTiming, &gM55SpectralDiagnostic};
		const bool ok = h1M55SpectralProcessFrame(&gM55Spectral.context,
			waveform, H1_WAVEFORM_ELEMENTS, hann, int(frame), &capture,
			H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, false);
		const bool canaries = gM55Spectral.powerCanaryBefore == kSpectralCanary &&
			gM55Spectral.powerCanaryAfter == ~kSpectralCanary;
		if (!canaries) {
			gM55BoundedLoop.canariesOk = 0;
			gM55BoundedLoop.status = 2;
			gM55BoundedLoop.lastStage = gM55SpectralDiagnostic.lastCompletedStage;
			break;
		}
		if (!ok) {
			gM55BoundedLoop.status = 1;
			gM55BoundedLoop.lastStage = gM55SpectralDiagnostic.lastCompletedStage;
			break;
		}
		++gM55BoundedLoop.completed;
		gM55BoundedLoop.lastStage = gM55SpectralDiagnostic.lastCompletedStage;
	}
	gM55BoundedLoop.totalCycles = k_cycle_get_64() - start;
	if (gM55BoundedLoop.status == 0 && gM55BoundedLoop.completed == requested) {
		gM55Spectral.timing = H1M55SpectralTiming{};
		gM55Spectral.timing.clockHz = sys_clock_hw_cycles_per_sec();
		gM55Spectral.timing.totalCycles = gM55BoundedLoop.totalCycles;
	}
}

void runM55CompleteFrontend(const H1UsbFrame &request)
{
	const uint32_t useCmsisPower = request.payloadLength >= 4
		? readLe32(request.payload) : 0;
	if (useCmsisPower > 1) {
		sendError(request, "INVALID_POWER_MODE", "use 0 for scalar or 1 for CMSIS mag squared");
		return;
	}
	gM55FrontendCommandCurrent = true;
	gM55FrontendExecution = H1M55FrontendExecution{
		H1M55FrontendState::Running, H1M55FrontendStage::FrontendEnter,
		H1M55FrontendStage::None, 0};
	gM55FrontendTiming = H1M55FrontendTiming{};
	gM55FrontendValid = false;
	gM55FrontendStatus = H1FrontendStatus::InvalidInput;
	gM55LoopValid = false;
	gM55LoopExitStep = 0;
	gM55BoundedLoop.valid = 0;
	static constexpr char kAck[] = "{\"ok\":true,\"accepted\":true}";
	if (!sendJson(request, kAck, sizeof(kAck) - 1)) return;
	const float *const waveform = h1UploadedWaveform();
	const bool captureStages = request.payloadLength >= 8 && readLe32(request.payload + 4) != 0;
	gM55FrontendStatus = h1RunFrontendM55(
		waveform, H1_WAVEFORM_ELEMENTS, h1FrontendScratch(),
		gM55FrontendRuntime, h1CurrentBoundaries().frontend,
		H1_FRONTEND_ELEMENTS, gM55FrontendTiming, gM55FrontendExecution,
		useCmsisPower != 0, captureStages);
	gM55FrontendValid = gM55FrontendStatus == H1FrontendStatus::Ok;
	if (!gM55FrontendValid && gM55FrontendExecution.state == H1M55FrontendState::Running) {
		gM55FrontendExecution.failedStage = gM55FrontendExecution.currentStage;
		gM55FrontendExecution.reason = static_cast<uint32_t>(gM55FrontendStatus);
		gM55FrontendExecution.state = H1M55FrontendState::Failed;
	}
}

void sendM55FrontendExecution(const H1UsbFrame &request)
{
	const auto stateName = [](H1M55FrontendState state) {
		switch (state) {
		case H1M55FrontendState::Idle: return "IDLE";
		case H1M55FrontendState::Running: return "RUNNING";
		case H1M55FrontendState::Success: return "SUCCESS";
		case H1M55FrontendState::Failed: return "FAILED";
		case H1M55FrontendState::Faulted: return "FAULTED";
		}
		return "UNKNOWN";
	};
	const auto stageName = [](H1M55FrontendStage stage) {
		switch (stage) {
		case H1M55FrontendStage::None: return "NONE";
		case H1M55FrontendStage::FrontendEnter: return "FRONTEND_ENTER";
		case H1M55FrontendStage::SpectralEnter: return "SPECTRAL_ENTER";
		case H1M55FrontendStage::SpectralDone: return "SPECTRAL_DONE";
		case H1M55FrontendStage::MelEnter: return "MEL_ENTER";
		case H1M55FrontendStage::MelDone: return "MEL_DONE";
		case H1M55FrontendStage::DbEnter: return "DB_ENTER";
		case H1M55FrontendStage::DbDone: return "DB_DONE";
		case H1M55FrontendStage::CropNormEnter: return "CROP_NORM_ENTER";
		case H1M55FrontendStage::CropNormDone: return "CROP_NORM_DONE";
		case H1M55FrontendStage::ResizeEnter: return "RESIZE_ENTER";
		case H1M55FrontendStage::ResizeDone: return "RESIZE_DONE";
		case H1M55FrontendStage::LayoutEnter: return "LAYOUT_ENTER";
		case H1M55FrontendStage::LayoutDone: return "LAYOUT_DONE";
		case H1M55FrontendStage::FrontendSuccess: return "FRONTEND_SUCCESS";
		}
		return "UNKNOWN";
	};
	char *response = h1ProtocolResponse();
	const int n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"current\":%s,\"state\":\"%s\",\"current_stage\":\"%s\",\"failed_stage\":\"%s\",\"reason\":%u,\"frontend_valid\":%s}",
		gM55FrontendCommandCurrent ? "true" : "false",
		stateName(gM55FrontendExecution.state),
		stageName(gM55FrontendExecution.currentStage),
		stageName(gM55FrontendExecution.failedStage),
		unsigned(gM55FrontendExecution.reason), gM55FrontendValid ? "true" : "false");
	if (n > 0 && size_t(n) < H1_PROTOCOL_RESPONSE_BYTES) sendJson(request, response, size_t(n));
	else sendError(request, "FORMAT_OVERFLOW", "Frontend execution status did not fit");
}

void sendM55FrontendData(const H1UsbFrame &request)
{
	if (!gM55FrontendValid) { sendError(request, "NO_FRONTEND_RESULT", "Run complete frontend first"); return; }
	const unsigned kind = readLe32(request.payload);
	const unsigned offset = readLe32(request.payload + 4);
	const unsigned count = readLe32(request.payload + 8);
	const unsigned length = kind == 0 ? H1_FRONTEND_ELEMENTS :
		(kind == 1 || kind == 2 ? 188 * 128 :
		 (kind == 3 ? 125 * 188 : (kind == 4 ? 224 * 281 : 0)));
	if (kind > 4 || count == 0 || count > 256 || offset > length || count > length - offset) {
		sendError(request, "INVALID_FRONTEND_RANGE", "kind 0..4, count 1..256, in-range offset required"); return;
	}
	const H1FrontendScratch &scratch = h1FrontendScratch();
	const float *data = kind == 0 ? h1CurrentBoundaries().frontend :
		(kind == 1 ? scratch.melRaw :
		 (kind == 2 ? scratch.melDb : (kind == 3 ? scratch.image : scratch.gray)));
	char *response = h1ProtocolResponse(); size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"kind\":%u,\"offset\":%u,\"values\":[", kind, offset);
	for (unsigned i = 0; ok && i < count; ++i)
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%s\"%08x\"", i ? "," : "", floatBits(data[offset + i]));
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
	if (ok) sendJson(request, response, used); else sendError(request, "FORMAT_OVERFLOW", "Frontend data chunk did not fit");
}

void sendM55SpectralDiagnostic(const H1UsbFrame &request)
{
	const H1M55SpectralDiagnosticRecord &record = gM55SpectralDiagnostic;
	const bool valid = h1M55SpectralDiagnosticValid(&record);
	char *response = h1ProtocolResponse();
	const int n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
		R"json({"ok":true,"valid":%s,"v":%u,"sequence":%u,"frame_index":%u,"stop_after":%u,"entered":%u,"completed":%u,"exception":%u,"exception_number":%u,"cfsr":%u,"hfsr":%u,"mmfar":%u,"bfar":%u,"pc":%u,"lr":%u,"xpsr":%u})json",
		valid ? "true" : "false",
		valid ? unsigned(record.version) : 0u,
		valid ? unsigned(record.commandSequence) : 0u,
		valid ? unsigned(record.frameIndex) : 0u,
		valid ? unsigned(record.stopAfterStage) : 0u,
		valid ? unsigned(record.lastEnteredStage) : 0u,
		valid ? unsigned(record.lastCompletedStage) : 0u,
		valid ? unsigned(record.exceptionReason) : 0u,
		valid ? unsigned(record.exceptionNumber) : 0u,
		valid ? unsigned(record.cfsr) : 0u,
		valid ? unsigned(record.hfsr) : 0u,
		valid ? unsigned(record.mmfar) : 0u,
		valid ? unsigned(record.bfar) : 0u,
		valid ? unsigned(record.stackedPc) : 0u,
		valid ? unsigned(record.stackedLr) : 0u,
		valid ? unsigned(record.stackedXpsr) : 0u);
	if (n <= 0 || size_t(n) >= H1_PROTOCOL_RESPONSE_BYTES) {
		static constexpr char kFormattingFailure[] = "{\"ok\":false}";
		sendJson(request, kFormattingFailure, sizeof(kFormattingFailure) - 1);
		return;
	}
	sendJson(request, response, size_t(n));
}

void sendM55SpectralTiming(const H1UsbFrame &request)
{
	const H1M55SpectralTiming &t = gM55Spectral.timing;
	char *response = h1ProtocolResponse();
	int n;
	if (gM55FrontendCommandCurrent) {
		const H1M55FrontendTiming &f = gM55FrontendTiming;
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"frontend\",\"clock_hz\":%u,"
			"\"total\":%llu,\"frame\":%llu,\"hann\":%llu,\"cfft\":%llu,\"split\":%llu,"
			"\"power\":%llu,\"mel\":%llu,\"db_log\":%llu,\"crop_norm\":%llu,"
			"\"resize\":%llu,\"layout\":%llu,\"elements\":%u,\"finite\":%u,"
			"\"crc32\":%u,\"dtype\":\"float32\",\"shape\":\"1,224,281,3\"}",
			unsigned(f.clockHz), (unsigned long long)f.totalCycles,
			(unsigned long long)f.frameCycles, (unsigned long long)f.hannCycles,
			(unsigned long long)f.cfftCycles, (unsigned long long)f.realSplitCycles,
			(unsigned long long)f.powerCycles, (unsigned long long)f.melCycles,
			(unsigned long long)f.dbLogCycles, (unsigned long long)f.cropNormalizeCycles,
			(unsigned long long)f.resizeCycles, (unsigned long long)f.finalLayoutCycles,
			unsigned(H1_FRONTEND_ELEMENTS), unsigned(f.finiteCount), unsigned(f.outputCrc32));
	} else if (gM55BoundedLoop.valid) {
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"bounded_loop\",\"frames_requested\":%u,"
			"\"frames_completed\":%u,\"status\":%u,\"last_frame\":%u,"
			"\"last_stage\":%u,\"total_cycles\":%llu,\"canaries_ok\":%u}",
			unsigned(gM55BoundedLoop.requested), unsigned(gM55BoundedLoop.completed),
			unsigned(gM55BoundedLoop.status), unsigned(gM55BoundedLoop.lastFrame),
			unsigned(gM55BoundedLoop.lastStage),
			(unsigned long long)gM55BoundedLoop.totalCycles,
			unsigned(gM55BoundedLoop.canariesOk));
	} else if (gM55LoopValid) {
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"loop188\",\"power_mode\":%u,\"clock_hz\":%u,"
			"\"total\":%llu,\"frame\":%llu,\"hann\":%llu,\"cfft\":%llu,\"split\":%llu,"
			"\"scalar\":%llu,\"cmsis\":%llu,\"min_frame\":%llu,\"max_frame\":%llu}",
			unsigned(gM55LoopPowerMode), unsigned(gM55LoopTiming.clockHz),
			(unsigned long long)gM55LoopTiming.totalCycles,
			(unsigned long long)gM55LoopTiming.framePreparationCycles,
			(unsigned long long)gM55LoopTiming.hannCycles,
			(unsigned long long)gM55LoopTiming.cfftCycles,
			(unsigned long long)gM55LoopTiming.realSplitCycles,
			(unsigned long long)gM55LoopTiming.powerSquaresCycles,
			(unsigned long long)gM55LoopTiming.powerCmsisMagSquaredCycles,
			(unsigned long long)gM55LoopMinimumCycles,
			(unsigned long long)gM55LoopMaximumCycles);
	} else if (gM55LoopExitStep != 0) {
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"loop_failed\",\"step\":%u}",
			unsigned(gM55LoopExitStep));
	} else if (!gM55FrontendValid && gM55FrontendTiming.clockHz != 0) {
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"frontend_failed\",\"status\":%u}",
			unsigned(gM55FrontendStatus));
	} else {
		n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
			"{\"ok\":true,\"mode\":\"frame\",\"clock_hz\":%u,"
			"\"total\":%llu,\"frame\":%llu,\"hann\":%llu,\"cfft\":%llu,\"split\":%llu,"
			"\"scalar\":%llu,\"cmsis\":%llu}",
			unsigned(t.clockHz), (unsigned long long)t.totalCycles,
			(unsigned long long)t.framePreparationCycles, (unsigned long long)t.hannCycles,
			(unsigned long long)t.cfftCycles, (unsigned long long)t.realSplitCycles,
			(unsigned long long)t.powerSquaresCycles,
			(unsigned long long)t.powerCmsisMagSquaredCycles);
	}
	if (n <= 0 || size_t(n) >= H1_PROTOCOL_RESPONSE_BYTES) {
		static constexpr char kFailure[] = "{\"ok\":false}";
		sendJson(request, kFailure, sizeof(kFailure) - 1);
		return;
	}
	sendJson(request, response, size_t(n));
}

void sendM55SpectralData(const H1UsbFrame &request)
{
	if (!gM55SpectralValid) { sendError(request, "NO_SPECTRAL_RESULT", "Run frame 0 first"); return; }
	const unsigned kind = readLe32(request.payload), offset = readLe32(request.payload + 4), count = readLe32(request.payload + 8);
	const unsigned length = kind <= 1 ? 2048 : 1025;
	if (kind > 5 || offset > length || count == 0 || count > 64 || count > length - offset) {
		sendError(request, "INVALID_SPECTRAL_RANGE", "kind 0..5, count 1..64, and in-range offset required"); return;
	}
	char *response = h1ProtocolResponse(); size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "{\"ok\":true,\"kind\":%u,\"offset\":%u,\"values\":[", kind, offset);
	for (unsigned i = 0; ok && i < count; ++i) ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%s\"%08x\"", i ? "," : "", spectralBits(kind, offset + i));
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
	if (ok) sendJson(request, response, used); else sendError(request, "FORMAT_OVERFLOW", "Spectral data chunk did not fit");
}

bool numericallyEqual(const H1RunResult &left, const H1RunResult &right)
{
	if (!left.valid || !right.valid || left.inputCrc32 != right.inputCrc32 ||
	    strcmp(left.inputSha256, right.inputSha256) != 0 ||
	    left.finiteCount != right.finiteCount ||
	    left.thresholdCount != right.thresholdCount ||
	    left.scoreCrc32 != right.scoreCrc32 || left.topCount != right.topCount ||
	    memcmp(left.boundaryCrc32, right.boundaryCrc32,
		   sizeof(left.boundaryCrc32)) != 0) {
		return false;
	}
	for (size_t index = 0; index < left.topCount; ++index) {
		if (left.top[index].index != right.top[index].index ||
		    left.top[index].scoreBits != right.top[index].scoreBits) {
			return false;
		}
	}
	return true;
}

void saveResult(H1RunResult &result)
{
	H1_VALIDATION_MARK(H1V_COMPARE_BEGIN);
	if (gLastResult.valid && result.valid &&
	    gLastResult.frontendNative == result.frontendNative &&
	    gLastResult.inputCrc32 == result.inputCrc32 &&
	    strcmp(gLastResult.inputSha256, result.inputSha256) == 0) {
		result.repeatComparable = 1;
		result.repeatEqual = numericallyEqual(gLastResult, result);
	}
	H1_VALIDATION_MARK(H1V_COMPARE_END);
	memcpy(&gLastResult, &result, sizeof(gLastResult));
	H1_VALIDATION_MARK(H1V_SAVE_COPY_END);
}

bool validateUpload(const H1UsbFrame &frame)
{
	memset(&gUpload, 0, sizeof(gUpload));
	gUpload.uploadSequence = frame.sequence;
	gUpload.frameCrcVerified = frame.actualPayloadCrc == frame.expectedPayloadCrc;
	if (frame.payloadLength != H1_UPLOAD_PAYLOAD_BYTES || !gUpload.frameCrcVerified) {
		return false;
	}
	const uint8_t *metadata = frame.payload;
	const uint32_t dtype = readLe32(metadata + 0);
	const uint32_t rank = readLe32(metadata + 4);
	const uint32_t dimension0 = readLe32(metadata + 8);
	const uint32_t dimension1 = readLe32(metadata + 12);
	const uint32_t sampleRate = readLe32(metadata + 16);
	const uint32_t rawBytes = readLe32(metadata + 20);
	const uint32_t declaredRawCrc = readLe32(metadata + 24);
	gUpload.metadataVerified = dtype == 1 && rank == 2 && dimension0 == 1 &&
		dimension1 == H1_WAVEFORM_ELEMENTS && sampleRate == 32000 &&
		rawBytes == H1_WAVEFORM_BYTES;
	bytesToHex(metadata + 28, 32, gUpload.declaredSha256);
	if (!gUpload.metadataVerified) {
		return false;
	}
	const uint8_t *raw = metadata + H1_UPLOAD_METADATA_BYTES;
	gUpload.rawCrc32 = crc32(raw, H1_WAVEFORM_BYTES);
	gUpload.rawCrcVerified = gUpload.rawCrc32 == declaredRawCrc;
	if (!gUpload.rawCrcVerified) {
		return false;
	}
	float *waveform = h1UploadedWaveform();
	memcpy(waveform, raw, H1_WAVEFORM_BYTES);
	for (size_t index = 0; index < H1_WAVEFORM_ELEMENTS; ++index) {
		gUpload.finiteCount += std::isfinite(waveform[index]);
	}
	gUpload.finiteVerified = gUpload.finiteCount == H1_WAVEFORM_ELEMENTS;
	if (!gUpload.finiteVerified) {
		return false;
	}
	gUpload.canonicalByteMatch =
		gUpload.rawCrc32 == H1_SYNTHETIC_WAVEFORM_CRC32 &&
		strcmp(gUpload.declaredSha256, H1_SYNTHETIC_RAW_SHA256) == 0 &&
		memcmp(waveform, h1SyntheticWaveformData, H1_WAVEFORM_BYTES) == 0;
	gUpload.valid = 1;
	return true;
}

bool sendJson(const H1UsbFrame &request, const char *json, size_t length)
{
	return h1UsbSendFrame(request.type | H1_CDC_RESPONSE_BIT, request.sequence, json,
			       uint32_t(length));
}

void sendError(const H1UsbFrame &request, const char *code, const char *detail)
{
	char *response = h1ProtocolResponse();
	const int length = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":false,\"code\":\"%s\",\"detail\":\"%s\"}", code,
		detail);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		h1UsbSendFrame(H1_CDC_ERROR_TYPE, request.sequence, response,
			       uint32_t(length));
	}
}

void sendPing(const H1UsbFrame &request)
{
#if H1_RAW_MEMORY_READ_DIAGNOSTIC
	if (gRawReadCompletionMarker == kRawReadDoneMagic) {
		static constexpr char kComplete[] =
			"{\"ok\":true,\"pong\":true,\"raw_read_done\":true}";
		sendJson(request, kComplete, sizeof(kComplete) - 1);
	} else {
		static constexpr char kPending[] =
			"{\"ok\":true,\"pong\":true,\"raw_read_done\":false}";
		sendJson(request, kPending, sizeof(kPending) - 1);
	}
	return;
#endif
	static constexpr char kResponse[] =
		"{\"ok\":true,\"pong\":true,\"protocol\":\"H1_CDC_PROTOCOL\","
		"\"protocol_version\":1}";
	sendJson(request, kResponse, sizeof(kResponse) - 1);
}

void sendPsramReady(const H1UsbFrame &request)
{
	const device *ram = DEVICE_DT_GET(DT_NODELABEL(aps512xxn));
	const bool ready = device_is_ready(ram);
	if (ready) {
		static constexpr char kReady[] =
			"{\"ok\":true,\"psram_ready\":true}";
		sendJson(request, kReady, sizeof(kReady) - 1);
	} else {
		static constexpr char kNotReady[] =
			"{\"ok\":true,\"psram_ready\":false}";
		sendJson(request, kNotReady, sizeof(kNotReady) - 1);
	}
}

void markInternalDiagnosticOnly(const H1UsbFrame &request)
{
	static_assert(offsetof(H1M55SpectralDiagnosticRecord, magic) % alignof(uint32_t) == 0);
	static_assert(offsetof(H1M55SpectralDiagnosticRecord, lastEnteredStage) % alignof(uint32_t) == 0);
	volatile uint32_t *const magic = &gM55SpectralDiagnostic.magic;
	volatile uint32_t *const stage = &gM55SpectralDiagnostic.lastEnteredStage;
	*magic = UINT32_C(0x4d41524b);
	*stage = UINT32_C(0x12345678);
	static constexpr char kAck[] = "{\"ok\":true}";
	sendJson(request, kAck, sizeof(kAck) - 1);
}

#if H1_RAW_MEMORY_READ_DIAGNOSTIC
uintptr_t rawReadProbeAddress(uint32_t probeId)
{
	switch (probeId) {
	case 0:
		return reinterpret_cast<uintptr_t>(&gRawReadSramProbeWord);
	case 1:
		return UINT32_C(0xa0000000);
	case 2:
		return UINT32_C(0xa1000000);
	case 3:
		return UINT32_C(0xa16a3180);
	case 4:
		return UINT32_C(0xa16a517c);
	case 5:
		return UINT32_C(0xa16a7180);
	case 6:
		return UINT32_C(0xa16ab200);
	case 7:
		return UINT32_C(0xa16ab230);
	default:
		return 0;
	}
}

void armRawRead(const H1UsbFrame &request)
{
	const uint32_t probeId = readLe32(request.payload);
	if (rawReadProbeAddress(probeId) == 0) {
		static constexpr char kInvalidProbe[] = "{\"ok\":false}";
		sendJson(request, kInvalidProbe, sizeof(kInvalidProbe) - 1);
		return;
	}
	gRawReadCompletionMarker = 0;
	gRawReadProbeId = probeId;
	gRawReadArmed = 1;
	static constexpr char kAck[] = "{\"ok\":true}";
	sendJson(request, kAck, sizeof(kAck) - 1);
}

void executeRawRead(const H1UsbFrame &)
{
	if (gRawReadArmed != 1 || gRawReadProbeId == kRawReadUnarmed) {
		return;
	}
	const uintptr_t address = rawReadProbeAddress(gRawReadProbeId);
	if (address == 0) {
		return;
	}
	const uint32_t observed =
		*reinterpret_cast<volatile const uint32_t *>(address);
	(void)observed;
	gRawReadCompletionMarker = kRawReadDoneMagic;
}
#endif

void sendStatus(const H1UsbFrame &request)
{
	const H1UsbStatus usb = h1UsbGetStatus();
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"scope\":\"H1_ENGINEERING_DEVELOPMENT\","
		"\"compute_ready\":%s,\"model_storage_verified\":%s,\"usb_initialized\":%s,\"usb_enabled\":%s,"
		"\"vbus_present\":%s,\"configured\":%s,\"dtr\":%s,"
		"\"bus_speed\":\"%s\",\"controller_maximum_speed\":\"%s\","
		"\"connection_generation\":%u,\"received_bytes\":%u,"
		"\"transmitted_bytes\":%u,\"last_stack_error\":%d,"
		"\"upload_valid\":%s,\"result_valid\":%s,\"profile_valid\":%s}",
		gComputeReady ? "true" : "false",
		gModelStorageVerified ? "true" : "false",
		usb.initialized ? "true" : "false",
		usb.enabled ? "true" : "false", usb.vbusPresent ? "true" : "false",
		usb.configured ? "true" : "false", usb.dtr ? "true" : "false",
		h1UsbSpeedName(usb.busSpeed), h1UsbSpeedName(usb.controllerMaximumSpeed),
		usb.connectionGeneration, usb.receivedBytes, usb.transmittedBytes,
		int(usb.lastStackError), gUpload.valid ? "true" : "false",
		gLastResult.valid ? "true" : "false",
		gLastResult.profile.valid ? "true" : "false");
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", "STATUS response did not fit");
	}
}

void compareNumericErrorSelfTest(const H1UsbFrame &request)
{
	if (request.payloadLength != 0) {
		sendError(request, "INVALID_ARGUMENT",
			  "CompareNumericErrorSelfTest takes no payload");
		return;
	}
	const float reference[] = {
		1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f,
		std::numeric_limits<float>::quiet_NaN(),
	};
	const float candidate[] = {
		1.0f, 2.5f, 1.0f, -1.0f, 9.0f, 6.0f, 8.0f,
		std::numeric_limits<float>::infinity(),
	};
	const int16_t referenceInt16[] = {0, 1, -1, 100, -100, 32767, -32768};
	const int16_t candidateInt16[] = {0, 2, -3, 98, -97, 32767, -32760};
	h1diag::FloatBufferErrorMetrics floating{};
	h1diag::Int16BufferErrorMetrics integer{};
	if (!h1diag::compareFloatBuffers(reference, candidate,
					 sizeof(reference) / sizeof(reference[0]), floating) ||
	    !h1diag::compareInt16Buffers(referenceInt16, candidateInt16,
					 sizeof(referenceInt16) / sizeof(referenceInt16[0]),
					 integer)) {
		sendError(request, "NUMERIC_COMPARISON_FAILED",
			  "Deterministic comparison buffers were rejected");
		return;
	}
	const bool knownVectorPass = floating.elementCount == 8 &&
		floating.referenceFiniteCount == 7 &&
		floating.candidateFiniteCount == 7 &&
		floating.finitePairCount == 7 &&
		floating.maximumAbsoluteError == 5.0 &&
		std::fabs(floating.meanAbsoluteError - (12.5 / 7.0)) < 1.0e-12 &&
		integer.elementCount == 7 && integer.mismatchedElements == 5 &&
		integer.maximumAbsoluteDelta == 8 && integer.deltaOneCount == 1 &&
		integer.deltaTwoCount == 2 && integer.deltaGreaterThanTwoCount == 2;
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":%s,\"command\":\"NUMERIC_ERROR_SELF_TEST\","
		"\"float\":{\"element_count\":%u,\"reference_finite_count\":%u,"
		"\"candidate_finite_count\":%u,\"finite_pair_count\":%u,"
		"\"mean_absolute_error\":%.12g,\"rms_error\":%.12g,"
		"\"relative_rms_error\":%.12g,\"max_absolute_error\":%.12g,"
		"\"reference_crc32\":\"%08x\",\"candidate_crc32\":\"%08x\"},"
		"\"int16\":{\"element_count\":%u,\"mismatched_elements\":%u,"
		"\"max_absolute_delta\":%u,\"delta_one_count\":%u,"
		"\"delta_two_count\":%u,\"delta_greater_than_two_count\":%u,"
		"\"reference_crc32\":\"%08x\",\"candidate_crc32\":\"%08x\"}}",
		knownVectorPass ? "true" : "false", floating.elementCount,
		floating.referenceFiniteCount, floating.candidateFiniteCount,
		floating.finitePairCount, floating.meanAbsoluteError,
		floating.rootMeanSquareError, floating.relativeRootMeanSquareError,
		floating.maximumAbsoluteError, floating.referenceCrc32,
		floating.candidateCrc32, integer.elementCount,
		integer.mismatchedElements, integer.maximumAbsoluteDelta,
		integer.deltaOneCount, integer.deltaTwoCount,
		integer.deltaGreaterThanTwoCount, integer.referenceCrc32,
		integer.candidateCrc32);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, static_cast<size_t>(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW",
			  "Numeric comparison report did not fit");
	}
}

void compareNativeSpectral(const H1UsbFrame &request)
{
	if (request.payloadLength != 0) {
		sendError(request, "INVALID_ARGUMENT",
			  "CompareNativeSpectral takes no payload");
		return;
	}

	constexpr uint32_t frameCount = H1_M55_FRAME_COUNT;
	constexpr uint32_t spectrumBins = H1_M55_RFFT_BINS;
	constexpr size_t valuesPerFrame = H1_M55_RFFT_BINS;
	const float *const waveform = h1SyntheticWaveformData;
	const float *nativeHann = nullptr;
	H1FrontendScratch &scratch = h1FrontendScratch();
	if (!h1GetFrontendHann(&nativeHann)) {
		sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
			  "failure_stage=HANN_CONSTANTS");
		return;
	}
	const H1FrontendStatus referenceInit =
		h1InitializeReferenceSpectral(scratch);
	if (referenceInit != H1FrontendStatus::Ok) {
		sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
			  "failure_stage=REFERENCE_FFT_INIT");
		return;
	}
	if (!h1M55SpectralPrepare(&gM55Spectral.context,
				  &h1M55SpectralWorkspace())) {
		sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
			  "failure_stage=NATIVE_FFT_INIT");
		return;
	}

	uint32_t referenceFinite = 0;
	uint32_t candidateFinite = 0;
	uint32_t finitePairs = 0;
	uint32_t framesCompleted = 0;
	uint32_t clockHz = 0;
	uint32_t referenceCrcState = UINT32_C(0xffffffff);
	uint32_t candidateCrcState = UINT32_C(0xffffffff);
	double absoluteErrorSum = 0.0;
	double errorSquares = 0.0;
	double referenceSquares = 0.0;
	double maximumAbsoluteError = 0.0;
	uint64_t totalCycles = 0;
	uint64_t framePreparationCycles = 0;
	uint64_t hannCycles = 0;
	uint64_t fftCycles = 0;
	uint64_t realSplitCycles = 0;
	uint64_t powerCycles = 0;

	auto updateCrc = [](uint32_t &state, const float *values, size_t elements) {
		const auto *const bytes = reinterpret_cast<const uint8_t *>(values);
		for (size_t index = 0; index < elements * sizeof(float); ++index) {
			state ^= bytes[index];
			for (unsigned bit = 0; bit < 8; ++bit) {
				state = (state >> 1) ^
					(UINT32_C(0xedb88320) & (0u - (state & 1u)));
			}
		}
	};

	for (uint32_t frame = 0; frame < frameCount; ++frame) {
		if (h1RunReferenceSpectralFrame(
			    waveform, H1_WAVEFORM_ELEMENTS, scratch,
			    static_cast<int>(frame)) != H1FrontendStatus::Ok) {
			sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
				  "failure_stage=REFERENCE_FRAME");
			return;
		}

		H1M55SpectralTiming timing{};
		H1M55SpectralCapture capture{
			nullptr, nullptr, nullptr, gM55Spectral.power, &timing, nullptr};
		const uint64_t nativeStart = k_cycle_get_64();
		const bool nativeOk = h1M55SpectralProcessFrame(
			&gM55Spectral.context, waveform, H1_WAVEFORM_ELEMENTS,
			nativeHann, static_cast<int>(frame), &capture,
			H1_M55_STAGE_POWER_CMSIS_MAG_SQUARED, false);
		const uint64_t nativeEnd = k_cycle_get_64();
		if (!nativeOk) {
			sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
				  "failure_stage=NATIVE_SPECTRAL_FRAME");
			return;
		}
		if (clockHz == 0) {
			clockHz = timing.clockHz;
		}
		totalCycles += nativeEnd - nativeStart;
		framePreparationCycles += timing.framePreparationCycles;
		hannCycles += timing.hannCycles;
		fftCycles += timing.cfftCycles;
		realSplitCycles += timing.realSplitCycles;
		powerCycles += timing.powerCmsisMagSquaredCycles;

		h1diag::FloatBufferErrorMetrics frameMetrics{};
		if (!h1diag::compareFloatBuffers(scratch.power, gM55Spectral.power,
						 valuesPerFrame, frameMetrics)) {
			sendError(request, "SPECTRAL_DIAGNOSTIC_FAILED",
				  "failure_stage=NUMERIC_COMPARISON");
			return;
		}
		referenceFinite += frameMetrics.referenceFiniteCount;
		candidateFinite += frameMetrics.candidateFiniteCount;
		finitePairs += frameMetrics.finitePairCount;
		updateCrc(referenceCrcState, scratch.power, valuesPerFrame);
		updateCrc(candidateCrcState, gM55Spectral.power, valuesPerFrame);
		for (size_t bin = 0; bin < valuesPerFrame; ++bin) {
			const float reference = scratch.power[bin];
			const float candidate = gM55Spectral.power[bin];
			if (!std::isfinite(reference) || !std::isfinite(candidate)) {
				continue;
			}
			const double referenceValue = static_cast<double>(reference);
			const double error = std::fabs(
				referenceValue - static_cast<double>(candidate));
			absoluteErrorSum += error;
			errorSquares += error * error;
			referenceSquares += referenceValue * referenceValue;
			if (error > maximumAbsoluteError) {
				maximumAbsoluteError = error;
			}
		}
		++framesCompleted;
	}

	const uint32_t totalValues = frameCount * spectrumBins;
	const double pairCount = finitePairs ? static_cast<double>(finitePairs) : 1.0;
	const double meanAbsoluteError = absoluteErrorSum / pairCount;
	const double rmsError = std::sqrt(errorSquares / pairCount);
	const double relativeRms = referenceSquares > 0.0
		? std::sqrt(errorSquares / referenceSquares) : 0.0;
	const bool allFinite = referenceFinite == totalValues &&
		candidateFinite == totalValues && finitePairs == totalValues;
	const bool numericPass = allFinite && relativeRms <= 1.0e-6 &&
		maximumAbsoluteError <= 2.0e-4;
	const double cyclesToUs = clockHz != 0
		? 1000000.0 / static_cast<double>(clockHz) : 0.0;
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"command\":\"COMPARE_NATIVE_SPECTRAL\","
		"\"executed\":%s,\"numeric_pass\":%s,\"failure_stage\":\"%s\","
		"\"power_implementation\":\"CMSIS arm_cmplx_mag_squared_f32 with scalar DC/Nyquist squares\","
		"\"reference_semantics\":\"tflm_signal RFFT, hypotf magnitude squared\","
		"\"frame_count\":%u,\"spectrum_bins\":%u,"
		"\"finite_reference\":%u,\"finite_candidate\":%u,\"finite_pairs\":%u,"
		"\"mae\":%.12g,\"rms\":%.12g,\"relative_rms\":%.12g,"
		"\"max_abs\":%.12g,\"reference_crc32\":\"%08x\","
		"\"candidate_crc32\":\"%08x\",\"clock_hz\":%u,"
		"\"total_cycles\":%llu,\"total_us\":%.6f,"
		"\"frame_preparation_cycles\":%llu,\"frame_preparation_us\":%.6f,"
		"\"hann_cycles\":%llu,\"hann_us\":%.6f,"
		"\"fft_cycles\":%llu,\"fft_us\":%.6f,"
		"\"real_split_cycles\":%llu,\"real_split_us\":%.6f,"
		"\"power_cycles\":%llu,\"power_us\":%.6f}",
		framesCompleted == frameCount ? "true" : "false",
		numericPass ? "true" : "false",
		numericPass ? "NONE" : (allFinite ? "NUMERICAL_THRESHOLDS" : "NONFINITE"),
		frameCount, spectrumBins, referenceFinite, candidateFinite, finitePairs,
		meanAbsoluteError, rmsError, relativeRms, maximumAbsoluteError,
		~referenceCrcState, ~candidateCrcState, clockHz,
		static_cast<unsigned long long>(totalCycles),
		static_cast<double>(totalCycles) * cyclesToUs,
		static_cast<unsigned long long>(framePreparationCycles),
		static_cast<double>(framePreparationCycles) * cyclesToUs,
		static_cast<unsigned long long>(hannCycles),
		static_cast<double>(hannCycles) * cyclesToUs,
		static_cast<unsigned long long>(fftCycles),
		static_cast<double>(fftCycles) * cyclesToUs,
		static_cast<unsigned long long>(realSplitCycles),
		static_cast<double>(realSplitCycles) * cyclesToUs,
		static_cast<unsigned long long>(powerCycles),
		static_cast<double>(powerCycles) * cyclesToUs);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, static_cast<size_t>(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW",
			  "Native spectral report did not fit");
	}
}

bool appendCompactMelMetrics(char *response, size_t capacity, size_t &used,
			     const char *name,
			     const H1M55CompactScalarMelMetrics &metrics)
{
	return append(response, capacity, used,
		"\"%s\":{\"elements\":%u,\"finite_reference\":%u,"
		"\"finite_candidate\":%u,\"finite_pairs\":%u,"
		"\"mae\":%.12g,\"rms\":%.12g,\"relative_rms\":%.12g,"
		"\"max_abs\":%.12g,\"exact_bit_matches\":%u,"
		"\"crc_equal\":%s,\"reference_crc32\":\"%08x\","
		"\"candidate_crc32\":\"%08x\"}",
		name, metrics.elementCount, metrics.finiteReference,
		metrics.finiteCandidate, metrics.finitePairs,
		metrics.meanAbsoluteError, metrics.rootMeanSquareError,
		metrics.relativeRootMeanSquareError, metrics.maximumAbsoluteError,
		metrics.exactBitMatches,
		metrics.referenceCrc32 == metrics.candidateCrc32 ? "true" : "false",
		metrics.referenceCrc32, metrics.candidateCrc32);
}

void compareCompactScalarMel(const H1UsbFrame &request)
{
	if (request.payloadLength != 0 || !gUpload.valid ||
	    !h1FrontendM55Ready(gM55FrontendRuntime)) {
		sendError(request, "COMPACT_MEL_REQUIRES_UPLOAD",
			  "Upload the canonical waveform and initialize the frontend first");
		return;
	}
	H1M55CompactScalarMelReport report{};
	if (!h1RunCompactScalarMelDiagnostic(
		    h1UploadedWaveform(), H1_WAVEFORM_ELEMENTS, h1FrontendScratch(),
		    gM55FrontendRuntime, report)) {
		sendError(request, "COMPACT_MEL_DIAGNOSTIC_FAILED",
			  "Reference, compact representation, or native spectral path failed");
		return;
	}
	char *response = h1ProtocolResponse();
	size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"frame_count\":%u,\"mel_bins\":%u,"
		"\"value_count\":%u,\"compact_weight_count\":%u,"
		"\"compact_structure_valid\":%s,"
		"\"compact_weight_identity_valid\":%s,\"clock_hz\":%u,"
		"\"dense_native_power_crc32\":\"%08x\","
		"\"compact_native_power_crc32\":\"%08x\",\"isolation\":{",
		report.frameCount, report.melBins, report.valueCount,
		report.compactWeightCount,
		report.compactStructureValid ? "true" : "false",
		report.compactWeightIdentityValid ? "true" : "false",
		report.clockHz, report.denseNativePowerCrc32,
		report.compactNativePowerCrc32);
	ok = ok && appendCompactMelMetrics(response, H1_PROTOCOL_RESPONSE_BYTES,
					 used, "metrics", report.isolation);
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"},\"end_to_end\":{");
	ok = ok && appendCompactMelMetrics(response, H1_PROTOCOL_RESPONSE_BYTES,
					 used, "metrics", report.endToEnd);
	const uint64_t summedCycles = report.spectralCycles + report.compactMelCycles;
	const double cyclesToUs = report.clockHz != 0
		? 1000000.0 / static_cast<double>(report.clockHz) : 0.0;
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"},\"timing\":{\"spectral_cycles\":%llu,"
		"\"spectral_us\":%.6f,\"compact_mel_cycles\":%llu,"
		"\"compact_mel_us\":%.6f,\"total_cycles\":%llu,"
		"\"total_us\":%.6f,\"construction_cycles\":0,"
		"\"construction_us\":0}}",
		static_cast<unsigned long long>(report.spectralCycles),
		static_cast<double>(report.spectralCycles) * cyclesToUs,
		static_cast<unsigned long long>(report.compactMelCycles),
		static_cast<double>(report.compactMelCycles) * cyclesToUs,
		static_cast<unsigned long long>(summedCycles),
		static_cast<double>(summedCycles) * cyclesToUs);
	if (ok) sendJson(request, response, used);
	else sendError(request, "FORMAT_OVERFLOW", "Compact mel response did not fit");
}

void compareMveCompactMel(const H1UsbFrame &request)
{
	if (request.payloadLength != 0 || !gUpload.valid ||
	    !h1FrontendM55Ready(gM55FrontendRuntime)) {
		sendError(request, "MVE_MEL_REQUIRES_UPLOAD",
			  "Upload the canonical waveform and initialize the frontend first");
		return;
	}
	H1M55MveCompactMelReport report{};
	if (!h1RunMveCompactMelDiagnostic(
		    h1UploadedWaveform(), H1_WAVEFORM_ELEMENTS,
		    h1FrontendScratch(), gM55FrontendRuntime, report)) {
		sendError(request, "MVE_MEL_DIAGNOSTIC_FAILED",
			  "Compact table, shared power, or mel output validation failed");
		return;
	}
	const H1M55CompactScalarMelMetrics &isolated = report.scalarVsMve;
	const H1M55CompactScalarMelMetrics &contextual = report.referenceVsMve;
	const double exactPercent = isolated.elementCount == 0 ? 0.0 :
		100.0 * static_cast<double>(isolated.exactBitMatches) /
		static_cast<double>(isolated.elementCount);
	const double cyclesToUs = 1000000.0 / static_cast<double>(report.clockHz);
	const double speedup = report.mveCycles == 0 ? 0.0 :
		static_cast<double>(report.scalarCycles) / report.mveCycles;
	const double reduction = report.scalarCycles == 0 ? 0.0 :
		100.0 * (static_cast<double>(report.scalarCycles) - report.mveCycles) /
		static_cast<double>(report.scalarCycles);
	char *response = h1ProtocolResponse();
	size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"implementation\":\"%s\",\"used_mve\":%s,"
		"\"shape\":[%u,%u],\"retained_weights\":%u,"
		"\"isolated\":{\"total_outputs\":%u,\"finite_scalar\":%u,"
		"\"finite_mve\":%u,\"finite_pairs\":%u,"
		"\"exact_bit_matches\":%u,\"exact_percent\":%.9f,"
		"\"mae\":%.12g,\"rms\":%.12g,\"relative_rms\":%.12g,"
		"\"max_abs\":%.12g,\"scalar_crc32\":\"%08x\","
		"\"mve_crc32\":\"%08x\",\"native_power_crc32\":\"%08x\","
		"\"max_index\":%u,\"max_frame\":%u,\"max_band\":%u,"
		"\"max_scalar\":%.9g,\"max_mve\":%.9g,\"bit_identical\":%s},"
		"\"contextual\":{\"finite_reference\":%u,\"finite_mve\":%u,"
		"\"exact_matches\":%u,\"mae\":%.12g,\"rms\":%.12g,"
		"\"relative_rms\":%.12g,\"max_abs\":%.12g,"
		"\"reference_crc32\":\"%08x\",\"mve_crc32\":\"%08x\","
		"\"native_power_crc32\":\"%08x\",\"threshold\":5e-6,"
		"\"pass\":%s},\"timing\":{\"clock_hz\":%u,"
		"\"scalar_cycles\":%llu,\"scalar_us\":%.6f,"
		"\"mve_cycles\":%llu,\"mve_us\":%.6f,"
		"\"speedup\":%.9f,\"cycle_reduction_percent\":%.6f}}",
		report.usedMve ? "mve_f32_four_frame" : "scalar_fallback",
		report.usedMve ? "true" : "false", report.frames, report.bands,
		report.retainedWeights, isolated.elementCount,
		isolated.finiteReference, isolated.finiteCandidate, isolated.finitePairs,
		isolated.exactBitMatches, exactPercent, isolated.meanAbsoluteError,
		isolated.rootMeanSquareError, isolated.relativeRootMeanSquareError,
		isolated.maximumAbsoluteError, isolated.referenceCrc32,
		isolated.candidateCrc32, report.nativePowerCrc32,
		report.maximumErrorIndex, report.maximumErrorFrame,
		report.maximumErrorBand,
		static_cast<double>(report.maximumErrorScalarValue),
		static_cast<double>(report.maximumErrorMveValue),
		isolated.exactBitMatches == isolated.elementCount ? "true" : "false",
		contextual.finiteReference, contextual.finiteCandidate,
		contextual.exactBitMatches, contextual.meanAbsoluteError,
		contextual.rootMeanSquareError,
		contextual.relativeRootMeanSquareError,
		contextual.maximumAbsoluteError, contextual.referenceCrc32,
		contextual.candidateCrc32, report.nativePowerCrc32,
		contextual.relativeRootMeanSquareError < 5.0e-6 ? "true" : "false",
		report.clockHz,
		static_cast<unsigned long long>(report.scalarCycles),
		static_cast<double>(report.scalarCycles) * cyclesToUs,
		static_cast<unsigned long long>(report.mveCycles),
		static_cast<double>(report.mveCycles) * cyclesToUs,
		speedup, reduction);
	if (ok) sendJson(request, response, used);
	else sendError(request, "FORMAT_OVERFLOW", "MVE mel report did not fit");
}

void verifyModelStorage(const H1UsbFrame &request)
{
	if (request.payloadLength != 0) {
		sendError(request, "INVALID_ARGUMENT", "VerifyModelStorage takes no payload");
		return;
	}
	gModelStorageVerified = false;
	struct ModelIdentity {
		uint32_t address;
		uint32_t bytes;
		uint32_t expectedCrc;
		const char *expectedSha;
		uint32_t observedCrc;
		char observedSha[65];
		bool crcMatch;
		bool shaMatch;
	};
	ModelIdentity models[] = {
		{H1_BACKBONE_FLASH_ADDRESS, H1_BACKBONE_MODEL_BYTES,
		 H1_BACKBONE_MODEL_CRC32, H1_BACKBONE_MODEL_SHA256, 0, {}, false, false},
		{H1_CLASSIFIER_FLASH_ADDRESS, H1_CLASSIFIER_MODEL_BYTES,
		 H1_CLASSIFIER_MODEL_CRC32, H1_CLASSIFIER_MODEL_SHA256, 0, {}, false, false},
	};
	for (ModelIdentity &model : models) {
		printk("H1_OSPI_IDENTITY_BEGIN address=0x%08x bytes=%u read_only=1\n",
		       model.address, model.bytes);
		const auto *data = reinterpret_cast<const uint8_t *>(model.address);
		model.observedCrc = crc32(data, model.bytes);
		h1Sha256Hex(data, model.bytes, model.observedSha);
		model.crcMatch = model.observedCrc == model.expectedCrc;
		model.shaMatch = model.observedSha[0] != '\0' &&
			strcmp(model.observedSha, model.expectedSha) == 0;
		printk("H1_OSPI_IDENTITY_DONE address=0x%08x crc32=%08x sha256=%s "
		       "crc_match=%u sha_match=%u\n",
		       model.address, model.observedCrc, model.observedSha,
		       unsigned(model.crcMatch), unsigned(model.shaMatch));
	}
	gModelStorageVerified = models[0].crcMatch && models[0].shaMatch &&
		models[1].crcMatch && models[1].shaMatch;
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"backbone\":{\"address\":\"0x%08x\","
		"\"bytes\":%u,\"crc32\":\"%08x\",\"sha256\":\"%s\","
		"\"crc_match\":%s,\"sha_match\":%s},"
		"\"classifier\":{\"address\":\"0x%08x\",\"bytes\":%u,"
		"\"crc32\":\"%08x\",\"sha256\":\"%s\","
		"\"crc_match\":%s,\"sha_match\":%s},"
		"\"all_models_verified\":%s}",
		models[0].address, models[0].bytes, models[0].observedCrc,
		models[0].observedSha, models[0].crcMatch ? "true" : "false",
		models[0].shaMatch ? "true" : "false", models[1].address,
		models[1].bytes, models[1].observedCrc, models[1].observedSha,
		models[1].crcMatch ? "true" : "false",
		models[1].shaMatch ? "true" : "false",
		gModelStorageVerified ? "true" : "false");
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		gModelStorageVerified = false;
		sendError(request, "FORMAT_OVERFLOW", "model identity response did not fit");
	}
}

void sendIdentity(const H1UsbFrame &request)
{
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"scope\":\"H1_ENGINEERING_DEVELOPMENT\","
		"\"candidate\":\"M4_RANGE_COVERING_V3\",\"m4\":\"ACCEPTED\","
		"\"canonical_m5_input\":\"M4_RANGE_COVERING_V3\",\"formal_m5_acceptance\":false,"
		"\"firmware_source_bundle_sha256\":\"%s\","
		"\"frontend_sha256\":\"%s\",\"source_backbone_sha256\":\"%s\","
		"\"compiled_backbone_sha256\":\"%s\",\"gem_sha256\":\"%s\","
		"\"bridge_policy_sha256\":\"%s\",\"bridge_implementation_sha256\":\"%s\","
		"\"source_classifier_sha256\":\"%s\","
		"\"compiled_classifier_sha256\":\"%s\",\"labels_sha256\":\"%s\","
		"\"sdk_alif_commit\":\"a524855a8b470fff0e3edb2c902a034d45ddf2a4\","
		"\"zephyr_commit\":\"3a2b84d96961b53431a78685c6ec0f0df4ddf347\","
		"\"zephyr_sdk\":\"0.17.0\",\"compiler\":\"gcc-12.2.0\","
		"\"ethos_u_driver\":\"0.16.0\",\"vela\":\"5.0.0\","
		"\"board\":\"alif_e8_dk/ae822fa0e5597xx0/rtss_hp\","
		"\"scheduler\":\"M55_FRONTEND,U85_BACKBONE,M55_GEM,U85_CLASSIFIER,M55_POSTPROCESS\","
		"\"protocol\":\"H1_CDC_PROTOCOL\",\"protocol_version\":1,"
		"\"usb_vid\":\"2fe3\",\"usb_pid\":\"0001\","
		"\"usb_product\":\"BirdNET H1 Dev CDC\",\"usb_serial\":\"H1DEV1\"}",
		H1_SOURCE_BUNDLE_SHA256, H1_FRONTEND_SHA256, H1_SOURCE_BACKBONE_SHA256,
		H1_COMPILED_BACKBONE_SHA256, H1_GEM_SHA256,
		H1_BRIDGE_POLICY_SHA256, H1_BRIDGE_IMPLEMENTATION_SHA256, H1_SOURCE_CLASSIFIER_SHA256,
		H1_COMPILED_CLASSIFIER_SHA256, H1_LABELS_SHA256);
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", "GET_IDENTITY response did not fit");
	}
}

void sendUploadResult(const H1UsbFrame &request)
{
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"dtype\":\"float32\",\"shape\":[1,96000],"
		"\"sample_rate_hz\":32000,\"raw_bytes\":384000,"
		"\"frame_crc_verified\":true,\"metadata_verified\":true,"
		"\"raw_crc_verified\":true,\"finite_verified\":true,"
		"\"raw_crc32\":\"%08x\",\"declared_raw_sha256\":\"%s\","
		"\"sha256_verification\":\"host_declared_board_echo\","
		"\"canonical_byte_match\":%s}",
		gUpload.rawCrc32, gUpload.declaredSha256,
		gUpload.canonicalByteMatch ? "true" : "false");
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", "UPLOAD response did not fit");
	}
}

bool sendRunResult(const H1UsbFrame &request, const H1RunResult &result)
{
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"run_sequence\":%u,\"result_ready\":true,"
		"\"profile_ready\":true,\"frontend_path\":\"%s\",\"fixture_identity\":\"%s\","
		"\"input_crc32\":\"%08x\",\"score_crc32\":\"%08x\","
		"\"top1_index\":%u,\"threshold_count\":%u,"
		"\"repeat_comparable\":%s,\"repeat_equal\":%s}",
		result.runSequence,
		result.frontendNative ? "native_m55_compact_mve" : "legacy_tflm_dense_scalar",
		result.canonicalSynthetic ? "canonical_synthetic" : fixtureName(result.fixture),
		result.inputCrc32, result.scoreCrc32, result.top[0].index,
		result.thresholdCount, result.repeatComparable ? "true" : "false",
		result.repeatEqual ? "true" : "false");
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		return sendJson(request, response, size_t(length));
	}
	sendError(request, "FORMAT_OVERFLOW", "RUN response did not fit");
	return false;
}

void sendTopK(const H1UsbFrame &request)
{
	if (!gLastResult.valid) {
		sendError(request, "NO_RESULT", "No completed H1 result is stored");
		return;
	}
	char *response = h1ProtocolResponse();
	size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"run_sequence\":%u,\"score_crc32\":\"%08x\","
		"\"top_k\":[",
		gLastResult.runSequence, gLastResult.scoreCrc32);
	for (size_t rank = 0; ok && rank < kReportedTopCount; ++rank) {
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
			"%s{\"rank\":%u,\"index\":%u,\"score_bits\":\"%08x\"}",
			rank == 0 ? "" : ",", unsigned(rank + 1),
			gLastResult.top[rank].index, gLastResult.top[rank].scoreBits);
	}
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
	if (ok) {
		sendJson(request, response, used);
	} else {
		sendError(request, "FORMAT_OVERFLOW", "GET_TOPK response did not fit");
	}
}

bool appendLifecycleProfile(char *response, size_t capacity, size_t &used,
			    const char *name,
			    const H1ModelLifecycleProfile &profile)
{
	bool ok = append(
		response, capacity, used,
		",\"%s_lifecycle\":{\"model_source_address\":\"%08x\","
		"\"model_destination_address\":\"%08x\",\"model_bytes\":%u,"
		"\"model_source_crc32\":\"%08x\",\"model_destination_crc32\":\"%08x\","
		"\"model_memcmp_result\":%d,\"arena_used_bytes\":%u,"
		"\"input_bytes\":%u,\"output_bytes\":%u,"
		"\"lifecycle_start_cycles\":%llu,\"lifecycle_end_cycles\":%llu,"
		"\"model_lifecycle\":{\"cycles\":%llu,\"us\":%u}",
		name, profile.model_source_address, profile.model_destination_address,
		profile.model_bytes, profile.model_source_crc32,
		profile.model_destination_crc32, profile.model_memcmp_result,
		profile.arena_used_bytes, profile.input_bytes, profile.output_bytes,
		(unsigned long long)profile.lifecycle_start_cycles,
		(unsigned long long)profile.lifecycle_end_cycles,
		(unsigned long long)profile.lifecycle_cycles, profile.lifecycle_us);
	ok = ok && append(
		response, capacity, used,
		",\"source_crc\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"copy\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"destination_crc\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"crc_total\":{\"cycles\":%llu,\"us\":%u}",
		profile.source_crc_count,
		(unsigned long long)profile.source_crc_cycles, profile.source_crc_us,
		profile.copy_count, (unsigned long long)profile.copy_cycles,
		profile.copy_us, profile.destination_crc_count,
		(unsigned long long)profile.destination_crc_cycles,
		profile.destination_crc_us, (unsigned long long)profile.crc_cycles,
		profile.crc_us);
	ok = ok && append(
		response, capacity, used,
		",\"memcmp_verify\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"flatbuffer_validate\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"runtime_init\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"allocate_tensors\":{\"count\":%u,\"cycles\":%llu,\"us\":%u}",
		profile.memcmp_count, (unsigned long long)profile.memcmp_cycles,
		profile.memcmp_us, profile.validate_count,
		(unsigned long long)profile.validate_cycles, profile.validate_us,
		profile.runtime_init_count,
		(unsigned long long)profile.runtime_init_cycles,
		profile.runtime_init_us, profile.allocate_tensors_count,
		(unsigned long long)profile.allocate_tensors_cycles,
		profile.allocate_tensors_us);
	ok = ok && append(
		response, capacity, used,
		",\"tensor_bind\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"input_copy\":{\"count\":%u,\"cycles\":%llu,\"us\":%u},"
		"\"cache_prepare\":{\"count\":%u,\"address\":\"%08x\",\"bytes\":%u,"
		"\"start_cycles\":%llu,\"end_cycles\":%llu,\"cycles\":%llu,\"us\":%u},"
		"\"output_copy\":{\"count\":%u,\"cycles\":%llu,\"us\":%u}}",
		profile.tensor_bind_count,
		(unsigned long long)profile.tensor_bind_cycles, profile.tensor_bind_us,
		profile.input_copy_count,
		(unsigned long long)profile.input_copy_cycles, profile.input_copy_us,
		profile.cache_prepare_count, profile.cache_prepare_address,
		profile.cache_prepare_bytes,
		(unsigned long long)profile.cache_prepare_start_cycles,
		(unsigned long long)profile.cache_prepare_end_cycles,
		(unsigned long long)profile.cache_prepare_cycles,
		profile.cache_prepare_us, profile.output_copy_count,
		(unsigned long long)profile.output_copy_cycles, profile.output_copy_us);
	return ok;
}

void sendProfile(const H1UsbFrame &request)
{
	if (!gLastResult.valid || !gLastResult.profile.valid) {
		sendError(request, "NO_PROFILE", "No valid compute profile is stored");
		return;
	}
	const H1RuntimeProfile &p = gLastResult.profile;
	char *response = h1ProtocolResponse();
	size_t used = 0;
	bool ok = append(
		response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"run_sequence\":%u,\"profile_version\":%u,"
		"\"valid\":true,\"clock_source\":\"k_cycle_get_64_cortex_m_systick_extension\","
		"\"clock_hz\":%u,\"transport_excluded\":true,"
		"\"pre_frontend_overhead_cycles\":%llu,\"pre_frontend_overhead_us\":%u,"
		"\"frontend_m55_cycles\":%llu,\"frontend_m55_us\":%u,"
		"\"frontend_to_backbone_quantize_cycles\":%llu,"
		"\"frontend_to_backbone_quantize_us\":%u,"
		"\"frontend_to_backbone_invoke_cycles\":%llu,"
		"\"frontend_to_backbone_invoke_us\":%u,"
		"\"backbone_submit_to_irq_cycles\":%llu,"
		"\"backbone_submit_to_irq_us\":%u,"
		"\"backbone_invoke_cycles\":%llu,\"backbone_invoke_us\":%u,"
		"\"backbone_to_gem_handoff_cycles\":%llu,"
		"\"backbone_to_gem_handoff_us\":%u,"
		"\"gem_m55_cycles\":%llu,\"gem_m55_us\":%u,"
		"\"embedding_to_classifier_quantize_cycles\":%llu,"
		"\"embedding_to_classifier_quantize_us\":%u,"
		"\"gem_to_classifier_handoff_cycles\":%llu,"
		"\"gem_to_classifier_handoff_us\":%u,"
		"\"classifier_submit_to_irq_cycles\":%llu,"
		"\"classifier_submit_to_irq_us\":%u,"
		"\"classifier_invoke_cycles\":%llu,\"classifier_invoke_us\":%u,"
		"\"classifier_to_postprocess_handoff_cycles\":%llu,"
		"\"classifier_to_postprocess_handoff_us\":%u,"
		"\"postprocess_m55_cycles\":%llu,\"postprocess_m55_us\":%u,"
		"\"total_compute_cycles\":%llu,\"total_compute_us\":%u,"
		"\"backbone_a_cycles\":%llu,\"backbone_b_cycles\":%llu,"
		"\"backbone_c_cycles\":%llu,\"backbone_d_cycles\":%llu,"
		"\"classifier_a_cycles\":%llu,\"classifier_b_cycles\":%llu,"
		"\"classifier_c_cycles\":%llu,\"classifier_d_cycles\":%llu,"
		"\"unassociated_command_count\":%u,\"unassociated_irq_count\":%u,"
		"\"unassociated_cache_prepare_count\":%u",
		gLastResult.runSequence, p.profile_version, p.clock_hz,
		(unsigned long long)p.pre_frontend_overhead_cycles,
		p.pre_frontend_overhead_us, (unsigned long long)p.frontend_cycles,
		p.frontend_us,
		(unsigned long long)p.frontend_to_backbone_quantize_cycles,
		p.frontend_to_backbone_quantize_us,
		(unsigned long long)p.frontend_to_backbone_invoke_cycles,
		p.frontend_to_backbone_invoke_us,
		(unsigned long long)p.backbone.submit_to_irq_cycles,
		p.backbone.submit_to_irq_us,
		(unsigned long long)p.backbone.invoke_cycles, p.backbone.invoke_us,
		(unsigned long long)p.backbone_to_gem_handoff_cycles,
		p.backbone_to_gem_handoff_us, (unsigned long long)p.gem_cycles,
		p.gem_us,
		(unsigned long long)p.embedding_to_classifier_quantize_cycles,
		p.embedding_to_classifier_quantize_us,
		(unsigned long long)p.gem_to_classifier_handoff_cycles,
		p.gem_to_classifier_handoff_us,
		(unsigned long long)p.classifier.submit_to_irq_cycles,
		p.classifier.submit_to_irq_us,
		(unsigned long long)p.classifier.invoke_cycles,
		p.classifier.invoke_us,
		(unsigned long long)p.classifier_to_postprocess_handoff_cycles,
		p.classifier_to_postprocess_handoff_us,
		(unsigned long long)p.postprocess_cycles, p.postprocess_us,
		(unsigned long long)p.total_compute_cycles, p.total_compute_us,
		(unsigned long long)p.backbone.invoke_start_cycles,
		(unsigned long long)p.backbone.command_start_cycles,
		(unsigned long long)p.backbone.irq_entry_cycles,
		(unsigned long long)p.backbone.invoke_end_cycles,
		(unsigned long long)p.classifier.invoke_start_cycles,
		(unsigned long long)p.classifier.command_start_cycles,
		(unsigned long long)p.classifier.irq_entry_cycles,
		(unsigned long long)p.classifier.invoke_end_cycles,
		p.unassociated_command_count, p.unassociated_irq_count,
		p.unassociated_cache_prepare_count);
	ok = ok && appendLifecycleProfile(
		response, H1_PROTOCOL_RESPONSE_BYTES, used, "backbone",
		p.backbone.lifecycle);
	ok = ok && appendLifecycleProfile(
		response, H1_PROTOCOL_RESPONSE_BYTES, used, "classifier",
		p.classifier.lifecycle);
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "}");
	if (ok) {
		sendJson(request, response, used);
	} else {
		sendError(request, "FORMAT_OVERFLOW", "GET_PROFILE response did not fit");
	}
}

void sendResultSummary(const H1UsbFrame &request)
{
	if (!gLastResult.valid) {
		sendError(request, "NO_RESULT", "No completed H1 result is stored");
		return;
	}
	char *response = h1ProtocolResponse();
	const int length = snprintf(
		response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"run_sequence\":%u,\"fixture_identity\":\"%s\","
		"\"input_sha256\":\"%s\",\"input_crc32\":\"%08x\","
		"\"finite_count\":%u,\"threshold_bits\":\"%08x\","
		"\"threshold_count\":%u,\"score_crc32\":\"%08x\","
		"\"boundary_crc32\":{\"frontend\":\"%08x\","
		"\"backbone_input\":\"%08x\",\"shared_feature\":\"%08x\","
		"\"embedding\":\"%08x\",\"classifier_input\":\"%08x\","
		"\"logits\":\"%08x\",\"scores\":\"%08x\"},"
		"\"backbone_saturated_low\":%u,\"backbone_saturated_high\":%u,"
		"\"classifier_saturated_low\":%u,\"classifier_saturated_high\":%u,"
		"\"repeat_comparable\":%s,\"repeat_equal\":%s,"
		"\"reporting_decision\":\"score_ge_threshold\"}",
		gLastResult.runSequence,
		gLastResult.canonicalSynthetic ? "canonical_synthetic"
					       : fixtureName(gLastResult.fixture),
		gLastResult.inputSha256, gLastResult.inputCrc32,
		gLastResult.finiteCount, unsigned(H1_REPORT_THRESHOLD_BITS),
		gLastResult.thresholdCount, gLastResult.scoreCrc32,
		gLastResult.boundaryCrc32[0], gLastResult.boundaryCrc32[1],
		gLastResult.boundaryCrc32[2], gLastResult.boundaryCrc32[3],
		gLastResult.boundaryCrc32[4], gLastResult.boundaryCrc32[5],
		gLastResult.boundaryCrc32[6], gLastResult.backboneSaturatedLow,
		gLastResult.backboneSaturatedHigh, gLastResult.classifierSaturatedLow,
		gLastResult.classifierSaturatedHigh,
		gLastResult.repeatComparable ? "true" : "false",
		gLastResult.repeatEqual ? "true" : "false");
	if (length > 0 && length < int(H1_PROTOCOL_RESPONSE_BYTES)) {
		sendJson(request, response, size_t(length));
	} else {
		sendError(request, "FORMAT_OVERFLOW", "GET_RESULT_SUMMARY response did not fit");
	}
}


void sendBaselineStatus(const H1UsbFrame &request)
{
 const uint64_t a = h1ProfileNow(), b = h1ProfileNow();
 char *response = h1ProtocolResponse();
 const int n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
  "{\"ok\":true,\"baseline_version\":2,\"running\":%u,\"mode\":%u,\"last_mode\":%u,"
  "\"error\":%u,\"warmups\":%u,\"measured\":%u,\"requested\":%u,\"clock_hz\":%u,"
  "\"counter_bits\":64,\"pmu_counters\":%u,\"monotonic_probe\":[%llu,%llu],\"systick_load\":%u,"
  "\"cache_selector\":%u,\"cache_capacity\":%u,\"diagnostic_address\":%u,"
  "\"diagnostic_bytes\":%u,\"profile_bytes\":%u,\"irq_before\":%u,\"irq_after\":%u,"
  "\"first_run\":%u,\"last_run\":%u,\"cfsr\":%u,\"hfsr\":%u,"
  "\"model_storage_verified\":%s,\"compute_ready\":%s}",
  unsigned(h1BaselineState.running), unsigned(h1BaselineState.mode), unsigned(h1BaselineState.last_mode),
  unsigned(h1BaselineState.error), h1BaselineState.warmups_completed,
  h1BaselineState.measured_completed, h1BaselineState.requested_samples,
  unsigned(sys_clock_hw_cycles_per_sec()), unsigned(h1BaselinePmuCounters()), (unsigned long long)a, (unsigned long long)b,
  unsigned(SysTick->LOAD), h1BaselineState.cache_selector, h1BaselineState.cache_capacity_bytes,
  unsigned(reinterpret_cast<uintptr_t>(&h1BaselineState)), unsigned(sizeof(h1BaselineState)),
  unsigned(sizeof(H1RuntimeProfile)), h1BaselineState.irq_before, h1BaselineState.irq_after,
  h1BaselineState.first_run_sequence, h1BaselineState.last_run_sequence,
  unsigned(SCB->CFSR), unsigned(SCB->HFSR), gModelStorageVerified ? "true" : "false",
  gComputeReady ? "true" : "false");
 if (n > 0 && size_t(n) < H1_PROTOCOL_RESPONSE_BYTES) sendJson(request, response, size_t(n));
 else sendError(request, "FORMAT_OVERFLOW", "Baseline status did not fit");
}
void runBaselineCampaign(const H1UsbFrame &request, uint32_t mode)
{
 if (!gComputeReady || !gModelStorageVerified || !gUpload.valid ||
     !gUpload.canonicalByteMatch || strcmp(gUpload.declaredSha256, H1_SYNTHETIC_RAW_SHA256)) {
  sendError(request, "BASELINE_PREFLIGHT", "Verified canonical resident waveform and models required"); return;
 }
#if defined(H1_POSTPROCESSING_OBSERVATION)
 if (mode == H1_BASELINE_DIAGNOSTIC) h1PostprocessObserverCampaignBegin(request.sequence);
#endif
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
 if (mode == H1_BASELINE_DIAGNOSTIC) h1ValidationObserverCampaignBegin(request.sequence);
#endif
 h1BaselineBeginCampaign(mode, request.sequence);
 h1BaselineState.irq_before = h1IrqCount;
 const unsigned warmups = mode == H1_BASELINE_OBSERVER ? 0u : 5u;
 const unsigned measured = mode == H1_BASELINE_ACCEPTANCE ? 100u :
  mode == H1_BASELINE_OBSERVER ? 1u : 20u;
 for (unsigned run = 0; run < warmups + measured && !h1BaselineState.error; ++run) {
  const int32_t index = run < warmups ? -1 : int32_t(run - warmups);
  H1RunResult result;
  h1BaselineBeginRun(index);
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
  h1ValidationObserverPrepare(mode == H1_BASELINE_DIAGNOSTIC);
#endif
  const uint64_t start = h1ProfileNow();
  H1_VALIDATION_AT(H1V_PRIMARY_START, start);
  h1RunStateBegin(request.sequence + run, gUpload.rawCrc32);
  h1RunStateMark(H1RunState::RunCommandReceived);
  const bool success = runOnce(FixtureId::Uploaded, h1UploadedWaveform(),
   gUpload.rawCrc32, gUpload.declaredSha256, gUpload.canonicalByteMatch, result);
  if (success) {
   saveResult(result);
   H1_VALIDATION_MARK(H1V_RESULT_READY_BEGIN);
   h1RunStateMark(H1RunState::ResultReady);
   H1_VALIDATION_MARK(H1V_RESULT_READY_END);
  }
  const uint64_t end = h1ProfileNow();
  const bool integrity = success && result.valid && (!result.repeatComparable || result.repeatEqual);
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
  h1ValidationObserverCapture(index, end, result.runSequence, result.profile.clock_hz, success);
#endif
  h1BaselineEndRun(index, start, end, &result.profile, result.runSequence,
   uint32_t(result.status), result.boundaryCrc32, integrity);
#if defined(H1_POSTPROCESSING_OBSERVATION)
  if (mode == H1_BASELINE_DIAGNOSTIC) h1PostprocessObserverCapture(index);
#endif
 }
 h1BaselineState.irq_after = h1IrqCount;
 if (h1BaselineState.warmups_completed != warmups || h1BaselineState.measured_completed != measured ||
     h1BaselineState.irq_after - h1BaselineState.irq_before != 2u * (warmups + measured) ||
     SCB->CFSR || SCB->HFSR) h1BaselineState.error = 9;
 h1BaselineEndCampaign();
 sendBaselineStatus(request);
}
void sendBaselineSample(const H1UsbFrame &request)
{
 const uint32_t index = readLe32(request.payload);
 if (h1BaselineState.running || index >= h1BaselineState.measured_completed) {
  sendError(request, "BASELINE_SAMPLE_RANGE", "Completed campaign and valid sample index required"); return;
 }
 char *response = h1ProtocolResponse(); size_t used = 0;
 bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
  "{\"ok\":true,\"index\":%u,\"mode\":%u,\"clock_hz\":%u,\"cycles\":%llu,\"boundary_crc\":[",
  index, h1BaselineState.last_mode, h1BaselineState.clock_hz,
  (unsigned long long)h1BaselineState.samples[index]);
 for (unsigned i = 0; ok && i < 7; ++i) ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
  "%s%u", i ? "," : "", h1BaselineState.boundary_crc[index][i]);
 ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]");
 if (h1BaselineState.last_mode != H1_BASELINE_ACCEPTANCE) {
  const H1BaselineDiagnostic &d = h1BaselineState.diagnostic[index];
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
   ",\"run\":%u,\"status\":%u,\"start\":%llu,\"end\":%llu,\"residual\":%llu,\"stages\":[",
   d.run_sequence, d.status, (unsigned long long)d.primary_start,
   (unsigned long long)d.primary_end, (unsigned long long)d.residual_cycles);
  for (unsigned i = 0; ok && i < 12; ++i) ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
   "%s%llu", i ? "," : "", (unsigned long long)d.stages[i]);
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"pmu\":[");
  for (unsigned stage = 0; ok && stage < 2; ++stage) {
   const H1BaselinePmu &p = d.pmu[stage];
   ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
    "%s{\"before_cycles\":%llu,\"after_cycles\":%llu,\"overflow\":%u,\"configured\":%u,\"snapshots\":%u,\"before\":[",
    stage ? "," : "", (unsigned long long)p.before_cycles, (unsigned long long)p.after_cycles,
    p.overflow, p.configured, p.snapshots);
   for (unsigned i = 0; ok && i < H1_BASELINE_EVENTS; ++i) ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
    "%s%u", i ? "," : "", p.before[i]);
   ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"after\":[");
   for (unsigned i = 0; ok && i < H1_BASELINE_EVENTS; ++i) ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
    "%s%u", i ? "," : "", p.after[i]);
   ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
  }
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"cache\":[");
  for (unsigned i = 0; ok && i < d.cache_count; ++i) {
   const H1BaselineCache &c = d.cache[i];
   ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
    "%s{\"stage\":%u,\"address\":%u,\"requested\":%u,\"rounded_address\":%u,\"rounded_bytes\":%u,"
    "\"maintained\":%u,\"operation_mask\":%u,\"mask\":%u,\"base_index\":%u,\"start\":%llu,\"end\":%llu}",
    i ? "," : "", c.stage, c.address, c.requested_bytes, c.rounded_address,
    c.rounded_bytes, c.maintained_bytes, c.flags, c.mask, c.base_index,
    (unsigned long long)c.start_cycles, (unsigned long long)c.end_cycles);
  }
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"events\":[");
  for (unsigned i = 0; ok && i < d.event_count; ++i) {
   const H1BaselineEvent &e = d.events[i];
   ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
    "%s{\"seq\":%u,\"kind\":%u,\"stage\":%u,\"cycles\":%llu,\"value\":%u}",
    i ? "," : "", i + 1, unsigned(e.kind), unsigned(e.stage),
    (unsigned long long)e.cycles, e.value);
  }
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"profile_le_hex\":\"");
  const auto *raw = reinterpret_cast<const uint8_t *>(&d.profile);
  for (size_t i = 0; ok && i < sizeof(d.profile); ++i)
   ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%02x", raw[i]);
  ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "\"");
 }
 ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "}");
 if (!ok) {
  sendError(request, "FORMAT_OVERFLOW", "Baseline sample did not fit");
 } else if (h1BaselineState.last_mode == H1_BASELINE_ACCEPTANCE) {
  if (!sendJson(request, response, used))
   sendError(request, "DIAGNOSTIC_EXPORT", "Baseline sample transmission failed");
 } else if (!h1UsbSendDiagnosticPages(request.type | H1_CDC_RESPONSE_BIT,
              request.sequence, response, uint32_t(used))) {
  sendError(request, "DIAGNOSTIC_EXPORT", "Diagnostic page transmission failed");
 }
}
void sendBaselineMap(const H1UsbFrame &request)
{
 H1Boundaries &b = h1CurrentBoundaries(); H1FrontendScratch &s = h1FrontendScratch();
 struct Entry { const char *name; const void *pointer; uint32_t bytes; const char *region; const char *lifetime; };
 const Entry entries[] = {
  {"waveform", h1UploadedWaveform(), H1_WAVEFORM_BYTES, "PSRAM_MODEL", "upload_to_campaign_end"},
  {"spectral_workspace", &h1M55SpectralWorkspace(), sizeof(H1M55SpectralWorkspace), "SRAM1", "one_frame"},
  {"power_group", s.gray, 4u * 1025u * 4u, "PSRAM_MODEL", "four_frames_until_compact_mel"},
  {"mel_db", s.melDb, sizeof(s.melDb), "PSRAM_MODEL", "compact_mel_to_crop"},
  {"image", s.image, sizeof(s.image), "PSRAM_MODEL", "crop_to_resize"},
  {"gray", s.gray, sizeof(s.gray), "PSRAM_MODEL", "resize_to_channel_layout"},
  {"frontend", b.frontend, sizeof(b.frontend), "PSRAM_MODEL", "frontend_to_quantization_and_ordinary_CRC"},
  {"backbone_boundary_input", b.backboneInput, sizeof(b.backboneInput), "PSRAM_MODEL", "quantization_to_copy_and_ordinary_CRC"},
  {"shared_feature", b.sharedFeature, sizeof(b.sharedFeature), "PSRAM_MODEL", "backbone_copy_to_GeM_and_ordinary_CRC"},
  {"gem_embedding_bridge_source", b.embedding, sizeof(b.embedding), "PSRAM_MODEL", "GeM_to_bridge_and_ordinary_CRC"},
  {"classifier_boundary_input", b.classifierInput, sizeof(b.classifierInput), "PSRAM_MODEL", "bridge_to_copy_and_ordinary_CRC"},
  {"logits", b.logits, sizeof(b.logits), "PSRAM_MODEL", "classifier_copy_to_postprocess_and_ordinary_CRC"},
  {"scores", b.scores, sizeof(b.scores), "PSRAM_MODEL", "sigmoid_to_reporting_and_ordinary_CRC"},
  {"ordinary_result", &gLastResult, sizeof(gLastResult), "DTCM", "saveResult_to_next_publication"},
  {"frontend_model", h1FrontendModelData, H1_FRONTEND_MODEL_BYTES, "MRAM", "image_lifetime"},
  {"hann", gM55FrontendRuntime.hann, 2048u * 4u, "MRAM", "boot_to_shutdown"},
  {"compact_mel_weights", &h1M55CompactMel(), sizeof(H1M55CompactMel), "SRAM1", "boot_to_shutdown"},
  {"backbone_model_origin", reinterpret_cast<const void *>(H1_BACKBONE_FLASH_ADDRESS), H1_BACKBONE_MODEL_BYTES, "OSPI1", "persistent"},
  {"classifier_model_origin", reinterpret_cast<const void *>(H1_CLASSIFIER_FLASH_ADDRESS), H1_CLASSIFIER_MODEL_BYTES, "OSPI1", "persistent"},
  {"backbone_persistent_model", reinterpret_cast<const void *>(H1_PSRAM_BASE), H1_BACKBONE_MODEL_BYTES, "PSRAM_MODEL", "boot_to_shutdown"},
  {"classifier_persistent_model", reinterpret_cast<const void *>(H1_CLASSIFIER_PSRAM_ADDRESS), H1_CLASSIFIER_MODEL_BYTES, "PSRAM_MODEL", "boot_to_shutdown"},
  {"backbone_arena", h1TensorArena, H1_BACKBONE_ARENA_BYTES, "SRAM0", "boot_to_shutdown"},
  {"classifier_arena", reinterpret_cast<const void *>(H1_CLASSIFIER_ARENA_ADDRESS), H1_CLASSIFIER_ARENA_BYTES, "SRAM0", "boot_to_shutdown"},
  {"persistent_contexts", h1PersistentStages, sizeof(h1PersistentStages), "DTCM", "boot_to_shutdown"},
  {"fast_memory", h1FastMemory, H1_FAST_RESERVATION_BYTES, "SRAM0", "NPU_stage"},
  {"diagnostic_RAM", &h1BaselineState, sizeof(h1BaselineState), "SRAM1", "campaign_to_next_campaign"},
  {"inactive_mel_raw", s.melRaw, sizeof(s.melRaw), "PSRAM_MODEL", "inactive_captureStages_false"},
  {"inactive_reference_frame", s.frame, sizeof(s.frame), "PSRAM_MODEL", "inactive_legacy_route"},
  {"inactive_reference_spectrum", s.spectrum, sizeof(s.spectrum), "PSRAM_MODEL", "inactive_legacy_route"},
  {"inactive_reference_power", s.power, sizeof(s.power), "PSRAM_MODEL", "inactive_legacy_route"},
  {"inactive_reference_fft", s.fftState, sizeof(s.fftState), "PSRAM_MODEL", "inactive_legacy_route"}
 };
 char *response = h1ProtocolResponse(); size_t used = 0;
 bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "{\"ok\":true,\"buffers\":[");
 for (size_t i = 0; ok && i < sizeof(entries)/sizeof(entries[0]); ++i) {
  const Entry &e = entries[i]; const uint32_t address = uint32_t(reinterpret_cast<uintptr_t>(e.pointer));
  ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
   "%s{\"name\":\"%s\",\"address\":%u,\"bytes\":%u,\"address_alignment\":%u,\"region\":\"%s\",\"lifetime\":\"%s\"}",
   i ? "," : "", e.name, address, e.bytes, address & (0u - address), e.region, e.lifetime);
 }
 ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"npu_tensors\":[");
 for (unsigned i = 0; ok && i < 2; ++i) {
  const H1BaselineTensorMap &m = h1BaselineState.tensors[i];
  ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
   "%s{\"stage\":%u,\"input\":%u,\"input_bytes\":%u,\"output\":%u,\"output_bytes\":%u,\"arena_used\":%u}",
   i ? "," : "", i, m.input, m.input_bytes, m.output, m.output_bytes, m.arena_used_bytes);
 }
 ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"mel_weight_count\":%u}", h1M55CompactMel().weightCount);
 if (ok) sendJson(request, response, used); else sendError(request, "FORMAT_OVERFLOW", "Baseline map did not fit");
}

void runUsbCommand(const H1UsbFrame &request, bool uploaded,
		   bool legacyReference = false)
{
	if (!gComputeReady) {
		sendError(request, "COMPUTE_NOT_READY", "NPU or memory identity check failed");
		return;
	}
	if (uploaded && !gUpload.valid) {
		sendError(request, "NO_UPLOAD", "No verified uploaded waveform is stored");
		return;
	}
	h1RunStateBegin(request.sequence, uploaded ? gUpload.rawCrc32
							: H1_SYNTHETIC_WAVEFORM_CRC32);
	h1RunStateMark(H1RunState::RunCommandReceived);
	H1RunResult result;
	const bool success = uploaded
		? runOnce(FixtureId::Uploaded, h1UploadedWaveform(), gUpload.rawCrc32,
			  gUpload.declaredSha256, gUpload.canonicalByteMatch, result, legacyReference)
		: runOnce(FixtureId::Synthetic, h1SyntheticWaveformData,
			  H1_SYNTHETIC_WAVEFORM_CRC32, H1_SYNTHETIC_RAW_SHA256, true,
			  result);
	if (!success) {
		char detail[96];
		snprintf(detail, sizeof(detail), "H1 compute failed at %s",
			 runStatusName(result.status));
		sendError(request, "COMPUTE_FAILED", detail);
		return;
	}
	saveResult(result);
	h1RunStateMark(H1RunState::ResultReady);
	h1RunStateMark(H1RunState::UsbResponseBegin);
	if (sendRunResult(request, result)) {
		h1RunStateMark(H1RunState::UsbResponseDone);
	}
}

#if !defined(H1_DIAG_SKIP_PDM_INIT) || H1_DIAG_SKIP_PDM_INIT == 0
void runMicUsbCommand(const H1UsbFrame &request)
{
	h1RunStateBegin(request.sequence, 0);
	h1RunStateMark(H1RunState::MicRunCommandReceived);
	if (!gComputeReady) {
		sendError(request, "COMPUTE_NOT_READY",
			  "NPU or memory identity check failed");
		return;
	}
	uint32_t floatCrc32 = 0;
	char floatSha256[65]{};
	H1SelectedWindowInfo selected{};
	H1MicError micError = H1MicError::None;
	if (!h1AudioPdmPrepareSelectedWaveform(
		    h1UploadedWaveform(), H1_WAVEFORM_ELEMENTS, floatCrc32,
		    floatSha256, selected, micError)) {
		char detail[128];
		snprintf(detail, sizeof(detail),
			 "Selected microphone window preparation failed: %s",
			 h1MicErrorName(micError));
		sendError(request, "MIC_WINDOW_PREPARE_FAILED", detail);
		return;
	}

	h1AudioPdmSetInferenceState(H1MicInferenceState::Running);
	H1RunResult result;
	const bool success = runOnce(
		FixtureId::Microphone, h1UploadedWaveform(), floatCrc32,
		floatSha256, false, result);
	if (!success) {
		h1AudioPdmSetInferenceState(H1MicInferenceState::Idle);
		char detail[128];
		snprintf(detail, sizeof(detail),
			 "Microphone H1 failed at %s for window %llu",
			 runStatusName(result.status),
			 (unsigned long long)selected.sequence);
		sendError(request, "MIC_H1_FAILED", detail);
		return;
	}
	saveResult(result);
	h1AudioPdmSetInferenceState(H1MicInferenceState::ResultReady);
	h1RunStateMark(H1RunState::ResultReady);
	h1RunStateMark(H1RunState::UsbResponseBegin);
	if (sendRunResult(request, result)) {
		h1RunStateMark(H1RunState::UsbResponseDone);
	}
}

#endif

void sendInferenceData(const H1UsbFrame &request)
{
	if (!gInferenceDataValid || !gLastResult.valid) {
		sendError(request, "NO_INFERENCE_DATA", "Complete an inference first");
		return;
	}
	const unsigned kind = readLe32(request.payload);
	const unsigned offset = readLe32(request.payload + 4);
	const unsigned count = readLe32(request.payload + 8);
	const unsigned length = kind == 0 ? H1_FRONTEND_ELEMENTS :
		(kind == 1 ? H1_LOGIT_ELEMENTS : 0);
	if (kind > 1 || count == 0 || count > 256 || offset > length ||
	    count > length - offset) {
		sendError(request, "INVALID_INFERENCE_RANGE", "kind 0 frontend or 1 scores; count 1..256, in-range offset required");
		return;
	}
	const float *data = kind == 0 ? h1CurrentBoundaries().frontend :
		h1CurrentBoundaries().scores;
	char *response = h1ProtocolResponse();
	size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"run_sequence\":%u,\"kind\":%u,\"offset\":%u,\"values\":[",
		gLastResult.runSequence, kind, offset);
	for (unsigned i = 0; ok && i < count; ++i)
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
			"%s\"%08x\"", i ? "," : "", floatBits(data[offset + i]));
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
	if (ok) sendJson(request, response, used);
	else sendError(request, "FORMAT_OVERFLOW", "Inference data chunk did not fit");
}

#if defined(H1_POSTPROCESSING_OBSERVATION)
void sendPostprocessObservation(const H1UsbFrame &request)
{
	const uint32_t index = readLe32(request.payload);
	const auto &state = h1PostprocessObserverState;
	if (index != UINT32_MAX && (index >= 20 || index >= state.measuredStored)) {
		sendError(request, "NO_POSTPROCESS_SAMPLE", "Measured observer sample unavailable");
		return;
	}
	const auto &o = index == UINT32_MAX ? state.last : state.measured[index];
	char *response = h1ProtocolResponse();
	const int n = snprintf(response, H1_PROTOCOL_RESPONSE_BYTES,
		"{\"ok\":true,\"observer_version\":2,\"sample_index\":%u,\"campaign_sequence\":%u,"
		"\"measured_stored\":%u,\"run_sequence\":%u,\"clock_hz\":%u,\"valid\":%u,\"error\":%u,"
		"\"timestamps\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
		"\"p0_cycles\":%llu,\"p1_cycles\":%llu,\"p2_cycles\":%llu,\"p3_cycles\":%llu,\"residual_cycles\":%llu,"
		"\"logit_elements\":%u,\"expf_calls\":%u,\"score_stores\":%u,\"finite_checks\":%u,\"threshold_checks\":%u,"
		"\"better_score_comparisons\":%u,\"top_insertion_shifts\":%u,\"top_results_materialized\":%u,"
		"\"heap_classes_visited\":%u,\"heap_root_comparisons\":%u,\"heap_better_score_calls\":%u,"
		"\"heap_root_replacements\":%u,\"heap_sift_up_comparisons\":%u,\"heap_sift_down_comparisons\":%u,"
		"\"heap_swaps\":%u,\"heap_drain_removals\":%u}",
		index, state.campaignSequence, state.measuredStored, o.runSequence, o.clockHz, o.valid, o.error,
		(unsigned long long)o.totalStart, (unsigned long long)o.scoreStart,
		(unsigned long long)o.scoreEnd, (unsigned long long)o.topStart,
		(unsigned long long)o.topEnd, (unsigned long long)o.materialEnd,
		(unsigned long long)o.totalEnd, (unsigned long long)(o.totalEnd - o.totalStart),
		(unsigned long long)(o.scoreEnd - o.scoreStart),
		(unsigned long long)(o.topEnd - o.topStart),
		(unsigned long long)(o.materialEnd - o.topEnd),
		(unsigned long long)((o.totalEnd - o.totalStart) - (o.scoreEnd - o.scoreStart) -
			(o.topEnd - o.topStart) - (o.materialEnd - o.topEnd)),
		unsigned(H1_LOGIT_ELEMENTS), unsigned(H1_LOGIT_ELEMENTS), unsigned(H1_LOGIT_ELEMENTS),
		unsigned(H1_LOGIT_ELEMENTS), unsigned(H1_LOGIT_ELEMENTS), o.comparisons, o.shifts, o.selected,
		o.heap.classesVisited, o.heap.rootComparisons, o.heap.betterScoreCalls,
		o.heap.rootReplacements, o.heap.siftUpComparisons, o.heap.siftDownComparisons,
		o.heap.heapSwaps, o.heap.drainRemovals);
	if (n > 0 && size_t(n) < H1_PROTOCOL_RESPONSE_BYTES) sendJson(request, response, size_t(n));
	else sendError(request, "FORMAT_OVERFLOW", "Postprocess observer response did not fit");
}
#endif

#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
void sendHotPathValidationObservation(const H1UsbFrame &request)
{
	const uint32_t index = readLe32(request.payload);
	const auto &state = h1ValidationObserverState;
	if (h1BaselineState.running || index >= state.measuredStored || index >= 20) {
		sendError(request, "VALIDATION_OBSERVER_RANGE", "Completed diagnostic sample required");
		return;
	}
	const auto &o = state.measured[index];
	char *response = h1ProtocolResponse(); size_t used = 0;
	bool ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used,
		"{\"ok\":true,\"observer_version\":1,\"index\":%u,"
		"\"campaign_sequence\":%u,\"stored\":%u,\"run_sequence\":%u,"
		"\"clock_hz\":%u,\"valid\":%u,\"error\":%u,"
		"\"waveform_bytes\":%u,\"waveform_calls\":%u,\"timestamps\":[",
		index, state.campaignSequence, state.measuredStored, o.runSequence,
		o.clockHz, o.valid, o.error, o.waveformBytes, o.waveformCalls);
	for (uint32_t i = 0; ok && i < H1V_MARK_COUNT; ++i)
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%s%llu",
			i ? "," : "", (unsigned long long)o.timestamps[i]);
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"boundary_bytes\":[");
	for (uint32_t i = 0; ok && i < 7; ++i)
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%s%u", i ? "," : "", o.boundaryBytes[i]);
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "],\"boundary_calls\":[");
	for (uint32_t i = 0; ok && i < 7; ++i)
		ok = append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "%s%u", i ? "," : "", o.boundaryCalls[i]);
	ok = ok && append(response, H1_PROTOCOL_RESPONSE_BYTES, used, "]}");
	if (ok) sendJson(request, response, used);
	else sendError(request, "FORMAT_OVERFLOW", "Validation observer response did not fit");
}
#endif

void handleFrame(const H1UsbFrame &request)
{
	// Commands that can overwrite shared inference scratch invalidate the reader.
	switch (H1MessageType(request.type)) {
	case H1MessageType::RunUploadedWaveform:
	case H1MessageType::RunLegacyUploadedWaveform:
	case H1MessageType::RunCanonical:
	case H1MessageType::MicRunWindow:
	case H1MessageType::RunM55SpectralFrame:
	case H1MessageType::RunM55SpectralLoop:
	case H1MessageType::RunM55SpectralBoundedLoop:
	case H1MessageType::RunM55CompleteFrontend:
	case H1MessageType::CompareNativeSpectral:
	case H1MessageType::CompareCompactScalarMel:
	case H1MessageType::CompareMveCompactMel:
		gInferenceDataValid = false;
		break;
	default:
		break;
	}
	switch (H1MessageType(request.type)) {
	case H1MessageType::RunBaselineObserverQualification:
		runBaselineCampaign(request, H1_BASELINE_OBSERVER); break;
	case H1MessageType::RunBaselineAcceptance:
		runBaselineCampaign(request, H1_BASELINE_ACCEPTANCE); break;
	case H1MessageType::RunBaselineDiagnostic:
		runBaselineCampaign(request, H1_BASELINE_DIAGNOSTIC); break;
	case H1MessageType::RunBaselinePmu:
		runBaselineCampaign(request, H1_BASELINE_PMU); break;
	case H1MessageType::GetBaselineSample:
		sendBaselineSample(request); break;
	case H1MessageType::GetBaselineStatus:
		sendBaselineStatus(request); break;
	case H1MessageType::GetBaselineMap:
		sendBaselineMap(request); break;
	case H1MessageType::Ping:
		sendPing(request);
		break;
	case H1MessageType::PsramReady:
		sendPsramReady(request);
		break;
#if defined(H1_I2S3_ACQUISITION)
	case H1MessageType::I2sStatus:
	case H1MessageType::I2sStart:
	case H1MessageType::I2sSelectChannel:
	case H1MessageType::I2sRawRead:
	case H1MessageType::I2sWindowInfo:
	case H1MessageType::I2sWindowRead:
	case H1MessageType::I2sStop:
	case H1MessageType::I2sConfigure:
	case H1MessageType::I2sStartSingle:
	case H1MessageType::I2sWindowHistory:
		(void)h1AudioI2sHandle(request);
		break;
#endif
	case H1MessageType::Status:
		sendStatus(request);
		break;
	case H1MessageType::GetIdentity:
		sendIdentity(request);
		break;
	case H1MessageType::VerifyModelStorage:
		verifyModelStorage(request);
		break;
	case H1MessageType::CompareNumericErrorSelfTest:
		compareNumericErrorSelfTest(request);
		break;
	case H1MessageType::CompareNativeSpectral:
		compareNativeSpectral(request);
		break;
	case H1MessageType::CompareCompactScalarMel:
		compareCompactScalarMel(request);
		break;
	case H1MessageType::CompareMveCompactMel:
		compareMveCompactMel(request);
		break;
	case H1MessageType::UploadWaveform:
		if (validateUpload(request)) {
			sendUploadResult(request);
		} else {
			sendError(request, "UPLOAD_REJECTED",
				  "Frame, metadata, raw CRC, or finite-value validation failed");
		}
		break;
	case H1MessageType::RunLegacyUploadedWaveform:
		runUsbCommand(request, true, true);
		break;
	case H1MessageType::GetInferenceData:
		sendInferenceData(request);
		break;
	case H1MessageType::RunUploadedWaveform:
		runUsbCommand(request, true);
		break;
	case H1MessageType::RunCanonical:
		runUsbCommand(request, false);
		break;
	case H1MessageType::RunM55SpectralFrame:
		runM55SpectralFrame(request);
		break;
	case H1MessageType::RunM55SpectralLoop:
		runM55SpectralLoop(request);
		break;
	case H1MessageType::RunM55SpectralBoundedLoop:
		runM55SpectralBoundedLoop(request);
		break;
	case H1MessageType::RunM55CompleteFrontend:
		runM55CompleteFrontend(request);
		break;
	case H1MessageType::GetM55SpectralData:
		sendM55SpectralData(request);
		break;
	case H1MessageType::GetM55FrontendData:
		sendM55FrontendData(request);
		break;
	case H1MessageType::GetM55FrontendExecution:
		sendM55FrontendExecution(request);
		break;
	case H1MessageType::GetM55SpectralDiagnostic:
		sendM55SpectralDiagnostic(request);
		break;
	case H1MessageType::GetM55SpectralTiming:
		sendM55SpectralTiming(request);
		break;
	case H1MessageType::MarkOnly:
		markInternalDiagnosticOnly(request);
		break;
#if H1_RAW_MEMORY_READ_DIAGNOSTIC
	case H1MessageType::ArmRead:
		armRawRead(request);
		break;
	case H1MessageType::ExecuteRead:
		executeRawRead(request);
		break;
#endif
	case H1MessageType::GetTopK:
		sendTopK(request);
		break;
#if defined(H1_POSTPROCESSING_OBSERVATION)
	case H1MessageType::GetPostprocessObservation:
		sendPostprocessObservation(request);
		break;
#endif
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
	case H1MessageType::GetHotPathValidationObservation:
		sendHotPathValidationObservation(request); break;
#endif
	case H1MessageType::GetProfile:
		sendProfile(request);
		break;
	case H1MessageType::GetResultSummary:
		sendResultSummary(request);
		break;
#if !defined(H1_DIAG_SKIP_PDM_INIT) || H1_DIAG_SKIP_PDM_INIT == 0
	case H1MessageType::MicStatus:
	case H1MessageType::MicStart:
	case H1MessageType::MicStop:
	case H1MessageType::MicCaptureInfo:
	case H1MessageType::MicGetWindowInfo:
	case H1MessageType::MicGetWindowPcm:
		if (!h1MicUsbHandle(request)) {
			sendError(request, "MIC_COMMAND_FAILED",
				  "Microphone command dispatch failed");
		}
		break;
	case H1MessageType::MicRunWindow:
		runMicUsbCommand(request);
		break;
#endif
	case H1MessageType::GetRunState:
		(void)h1RunStateSendSnapshot(request.sequence);
		break;
	default:
		sendError(request, "UNKNOWN_COMMAND", "Unsupported H1 CDC message type");
		break;
	}
}

void handleProtocolError(const H1UsbFrame &frame)
{
	const char *detail = "Unknown framing error";
	switch (frame.error) {
	case H1UsbProtocolError::UnsupportedVersion:
		detail = "Unsupported protocol version";
		break;
	case H1UsbProtocolError::InvalidLength:
		detail = "Invalid payload length for message type";
		break;
	case H1UsbProtocolError::PayloadCrcMismatch:
		detail = "Payload CRC-32 mismatch";
		break;
	case H1UsbProtocolError::None:
		break;
	}
	sendError(frame, "PROTOCOL_ERROR", detail);
}

void reportResult(const H1RunResult &result)
{
	printk("H1_ENDPOINT fixture=%s run=%u finite=%u score_crc=%08x "
	       "threshold_bits=%08x threshold_count=%u tie_break=lower_index\n",
	       fixtureName(result.fixture), result.runSequence, result.finiteCount,
	       result.scoreCrc32, unsigned(H1_REPORT_THRESHOLD_BITS),
	       result.thresholdCount);
	for (size_t rank = 0; rank < result.topCount; ++rank) {
		printk("H1_TOP fixture=%s run=%u rank=%u index=%u score_bits=%08x\n",
		       fixtureName(result.fixture), result.runSequence, unsigned(rank + 1),
		       result.top[rank].index, result.top[rank].scoreBits);
	}
	const H1RuntimeProfile &p = result.profile;
	printk("H1_PROFILE version=%u clock_hz=%u transport_excluded=%u "
	       "frontend_m55_us=%u backbone_submit_to_irq_us=%u backbone_invoke_us=%u "
	       "backbone_to_gem_handoff_us=%u gem_m55_us=%u "
	       "gem_to_classifier_handoff_us=%u classifier_submit_to_irq_us=%u "
	       "classifier_invoke_us=%u postprocess_m55_us=%u total_compute_us=%u\n",
	       p.profile_version, p.clock_hz, p.transport_excluded, p.frontend_us,
	       p.backbone.submit_to_irq_us, p.backbone.invoke_us,
	       p.backbone_to_gem_handoff_us, p.gem_us, p.gem_to_classifier_handoff_us,
	       p.classifier.submit_to_irq_us, p.classifier.invoke_us, p.postprocess_us,
	       p.total_compute_us);
}

void checkpoint(FixtureId fixture, unsigned run, const char *name, const void *data,
		size_t bytes)
{
	printk("H1_CHECKPOINT fixture=%s run=%u tensor=%s bytes=%u crc=%08x\n",
	       fixtureName(fixture), run, name, unsigned(bytes),
	       crc32(static_cast<const uint8_t *>(data), bytes));
}

void dumpRaw(FixtureId fixture, unsigned run, const char *name, const char *dtype,
	     const char *shape, const void *pointer, size_t bytes)
{
	constexpr size_t chunkBytes = 128;
	const char digits[] = "0123456789abcdef";
	char hex[chunkBytes * 2 + 1];
	const auto *raw = static_cast<const uint8_t *>(pointer);
	printk("H1_RAW_BEGIN fixture=%s run=%u tensor=%s bytes=%u dtype=%s shape=%s "
	       "chunk=%u\n",
	       fixtureName(fixture), run, name, unsigned(bytes), dtype, shape,
	       unsigned(chunkBytes));
	unsigned sequence = 0;
	for (size_t offset = 0; offset < bytes; offset += chunkBytes, ++sequence) {
		const size_t count = std::min(chunkBytes, bytes - offset);
		for (size_t index = 0; index < count; ++index) {
			hex[2 * index] = digits[raw[offset + index] >> 4];
			hex[2 * index + 1] = digits[raw[offset + index] & 15];
		}
		hex[2 * count] = 0;
		printk("H1_RAW_CHUNK fixture=%s run=%u tensor=%s seq=%u offset=%u bytes=%u "
		       "crc=%08x hex=%s\n",
		       fixtureName(fixture), run, name, sequence, unsigned(offset),
		       unsigned(count), crc32(raw + offset, count), hex);
	}
	printk("H1_RAW_END fixture=%s run=%u tensor=%s bytes=%u chunks=%u crc=%08x\n",
	       fixtureName(fixture), run, name, unsigned(bytes), sequence, crc32(raw, bytes));
}

bool compareBoundary(FixtureId fixture, const char *name, const void *first,
		     const void *second, size_t bytes)
{
	const auto *left = static_cast<const uint8_t *>(first);
	const auto *right = static_cast<const uint8_t *>(second);
	size_t mismatches = 0;
	size_t firstMismatch = bytes;
	for (size_t index = 0; index < bytes; ++index) {
		if (left[index] != right[index]) {
			if (firstMismatch == bytes) {
				firstMismatch = index;
			}
			++mismatches;
		}
	}
	printk("H1_REPEAT fixture=%s tensor=%s bytes=%u mismatched_bytes=%u "
	       "first_mismatch=%d first_crc=%08x second_crc=%08x\n",
	       fixtureName(fixture), name, unsigned(bytes), unsigned(mismatches),
	       firstMismatch == bytes ? -1 : int(firstMismatch), crc32(left, bytes),
	       crc32(right, bytes));
	return mismatches == 0;
}

bool reportAndCompareBoundaries(FixtureId fixture, unsigned run, H1Boundaries &current,
				H1Boundaries &first)
{
	checkpoint(fixture, run, "frontend_output", current.frontend, H1_FRONTEND_BYTES);
	checkpoint(fixture, run, "backbone_integer_input", current.backboneInput,
		   H1_BACKBONE_INPUT_BYTES);
	checkpoint(fixture, run, "shared_feature", current.sharedFeature,
		   H1_SHARED_FEATURE_BYTES);
	checkpoint(fixture, run, "embedding", current.embedding, H1_EMBEDDING_BYTES);
	checkpoint(fixture, run, "classifier_integer_input", current.classifierInput,
		   H1_CLASSIFIER_INPUT_BYTES);
	checkpoint(fixture, run, "logits", current.logits, H1_LOGIT_BYTES);
	checkpoint(fixture, run, "scores", current.scores, H1_SCORE_BYTES);
	if (run == 1) {
		if (fixture == FixtureId::Synthetic) {
			dumpRaw(fixture, run, "frontend_output", "float32", "1,224,281,3",
				current.frontend, H1_FRONTEND_BYTES);
			dumpRaw(fixture, run, "backbone_integer_input", "int16",
				"1,224,281,3", current.backboneInput,
				H1_BACKBONE_INPUT_BYTES);
			dumpRaw(fixture, run, "shared_feature", "int16", "1,7,9,1280",
				current.sharedFeature, H1_SHARED_FEATURE_BYTES);
			dumpRaw(fixture, run, "embedding", "float32", "1,1280",
				current.embedding, H1_EMBEDDING_BYTES);
			dumpRaw(fixture, run, "classifier_integer_input", "int16", "1,1280",
				current.classifierInput, H1_CLASSIFIER_INPUT_BYTES);
		}
		dumpRaw(fixture, run, "logits", "int16", "1,11560", current.logits,
			H1_LOGIT_BYTES);
		dumpRaw(fixture, run, "scores", "float32", "1,11560", current.scores,
			H1_SCORE_BYTES);
		memcpy(&first, &current, sizeof(H1Boundaries));
		return true;
	}
	return compareBoundary(fixture, "frontend_output", first.frontend, current.frontend,
			       H1_FRONTEND_BYTES) &&
	       compareBoundary(fixture, "backbone_integer_input", first.backboneInput,
			       current.backboneInput, H1_BACKBONE_INPUT_BYTES) &&
	       compareBoundary(fixture, "shared_feature", first.sharedFeature,
			       current.sharedFeature, H1_SHARED_FEATURE_BYTES) &&
	       compareBoundary(fixture, "embedding", first.embedding, current.embedding,
			       H1_EMBEDDING_BYTES) &&
	       compareBoundary(fixture, "classifier_integer_input", first.classifierInput,
			       current.classifierInput, H1_CLASSIFIER_INPUT_BYTES) &&
	       compareBoundary(fixture, "logits", first.logits, current.logits,
			       H1_LOGIT_BYTES) &&
	       compareBoundary(fixture, "scores", first.scores, current.scores,
			       H1_SCORE_BYTES);
}

bool runUartFixture(FixtureId fixture, const float *waveform, uint32_t expectedCrc,
		    const char *sha256)
{
	H1Boundaries &current = h1CurrentBoundaries();
	H1Boundaries &first = h1FirstBoundaries();
	for (unsigned run = 1; run <= 2; ++run) {
		H1RunResult result;
		if (!runOnce(fixture, waveform, expectedCrc, sha256,
			     fixture == FixtureId::Synthetic, result)) {
			printk("H1_FAIL fixture=%s stage=%s\n", fixtureName(fixture),
			       runStatusName(result.status));
			return false;
		}
		saveResult(result);
		reportResult(result);
		if (!reportAndCompareBoundaries(fixture, run, current, first)) {
			return false;
		}
		printk("H1_COMPLETE fixture=%s run=%u chain=M55,U85,M55,U85,M55\n",
		       fixtureName(fixture), run);
	}
	printk("H1_FIXTURE_EXECUTED fixture=%s complete_runs=2 repeat_equal=1\n",
	       fixtureName(fixture));
	return true;
}

void handleUartFallback(unsigned char command)
{
	if (command != 'C' && command != 'G') {
		return;
	}
	printk("H1_UART_TRIGGER command=%c transport_excluded_from_compute=1\n", command);
	if (!gComputeReady) {
		printk("H1_UART_FAIL compute_not_ready=1\n");
		return;
	}
	if (command == 'C') {
		H1RunResult result;
		if (runOnce(FixtureId::Synthetic, h1SyntheticWaveformData,
			    H1_SYNTHETIC_WAVEFORM_CRC32, H1_SYNTHETIC_RAW_SHA256, true,
			    result)) {
			saveResult(result);
			reportResult(result);
			printk("H1_UART_COMPLETE command=C\n");
		} else {
			printk("H1_UART_FAIL command=C stage=%s\n",
			       runStatusName(result.status));
		}
		return;
	}
	const bool executed = runUartFixture(
		FixtureId::Synthetic, h1SyntheticWaveformData, H1_SYNTHETIC_WAVEFORM_CRC32,
		H1_SYNTHETIC_RAW_SHA256) &&
		runUartFixture(FixtureId::Wren, h1WrenWaveformData,
			       H1_WREN_WAVEFORM_CRC32, H1_WREN_RAW_SHA256);
	printk("H1_UART_COMPLETE command=G classification=%s\n",
	       executed ? "H1_INTEGRATED_EXECUTED" : "H1_INTEGRATED_BLOCKED");
}
} // namespace

extern "C" bool h1LifecycleReuseReady(void)
{
	return h1LifecycleInitializationComplete == 1 &&
		h1PersistentStages[0].state == H1ContextState::Ready &&
		h1PersistentStages[1].state == H1ContextState::Ready;
}

int main()
{
#if H1_FRAME_ACK_RETURN_DIAGNOSTIC
	/* Keep the internal noinit record untouched in the pure ACK control. */
	const bool retainedSpectralDiagnostic = false;
#else
	const bool retainedSpectralDiagnostic =
		h1M55SpectralDiagnosticValid(&gM55SpectralDiagnostic);
	if (!retainedSpectralDiagnostic) {
		h1M55SpectralDiagnosticInitialize(&gM55SpectralDiagnostic);
	}
#endif
	printk("H1_SPECTRAL_DIAG_BOOT valid=%u address=%p bytes=%u\n",
	       unsigned(retainedSpectralDiagnostic), &gM55SpectralDiagnostic,
	       unsigned(sizeof(gM55SpectralDiagnostic)));
	printk("H1_BOOT scope=H1_ENGINEERING_DEVELOPMENT candidate=M4_RANGE_COVERING_V3 "
	       "m4=ACCEPTED canonical_m5_input=M4_RANGE_COVERING_V3 formal_acceptance=NO\n");
	printk("H1_ID source_bundle=%s frontend=%s source_backbone=%s "
	       "compiled_backbone=%s gem=%s source_classifier=%s compiled_classifier=%s "
	       "labels=%s\n",
	       H1_SOURCE_BUNDLE_SHA256, H1_FRONTEND_SHA256, H1_SOURCE_BACKBONE_SHA256,
	       H1_COMPILED_BACKBONE_SHA256, H1_GEM_SHA256, H1_SOURCE_CLASSIFIER_SHA256,
	       H1_COMPILED_CLASSIFIER_SHA256, H1_LABELS_SHA256);
	printk("H1_BRIDGE policy=%s implementation=%s scale_bits=%08x "
	       "rule=EXACT_BINARY32_RATIO_RNE_INT16 zero_point=0\n",
	       H1_BRIDGE_POLICY_SHA256, H1_BRIDGE_IMPLEMENTATION_SHA256,
	       unsigned(H1_CLASSIFIER_INPUT_SCALE_BITS));
	printk("H1_RUNTIME cpu=M55_HP npu=U85_256 sdk_alif=a524855a8b470fff0e3edb2c902a034d45ddf2a4 "
	       "zephyr=3a2b84d96961b53431a78685c6ec0f0df4ddf347 "
	       "zephyr_sdk=0.17.0 compiler=gcc-12.2.0 driver=0.16.0 vela=5.0.0\n");
	printk("H1_ARCH mode=H1_ONLY canonical_fanout=1,7,9,1280 "
	       "producer_owned_until_all_consumers_complete=1 modes=H1_ONLY,H23_ONLY,H1_H23_FULL "
	       "h23_implemented=0\n");
	printk("H1_FRONTEND_IMPL kind=NATIVE_M55_FROZEN_GRAPH_SEMANTICS "
	       "fft=CMSIS_M55_RFFT_FLOAT compiler_fp=fno-fast-math,ffp-contract-off "
	       "source_model_crc=%08x fpscr=%08x\n",
	       unsigned(H1_FRONTEND_MODEL_CRC32), unsigned(__get_FPSCR()));
	printk("H1_MEMORY model_slot=0x%08x model_slot_bytes=%u arena=0x%08x "
	       "arena_bytes=%u fast=0x%08x fast_bytes=%u current_boundaries=%p "
	       "first_boundaries=%p frontend_scratch=%p upload_payload=%p "
	       "uploaded_waveform=%p\n",
	       unsigned(H1_PSRAM_BASE), unsigned(H1_MODEL_SLOT_BYTES),
	       unsigned(H1_ARENA_ADDRESS), unsigned(H1_ARENA_RESERVATION_BYTES),
	       unsigned(H1_FAST_ADDRESS), unsigned(H1_FAST_RESERVATION_BYTES),
	       &h1CurrentBoundaries(), &h1FirstBoundaries(), &h1FrontendScratch(),
	       h1UploadPayload(), h1UploadedWaveform());
	printk("H1_CACHE model_and_checkpoints=no_cache_psram "
	       "npu=driver_pre_submit_clean_post_completion_clean_invalidate "
	       "handoff=irq_completion_then_m55_copy\n");
	printk("H1_POSTPROCESS logits=int16 dequant_scale_bits=%08x sigmoid=expf "
	       "ranking=descending_lower_index_tie threshold_bits=%08x threshold_cmp=ge\n",
	       unsigned(H1_LOGIT_SCALE_BITS), unsigned(H1_REPORT_THRESHOLD_BITS));

	// Initialize immutable frontend constants, compact mel storage, and the
	// production CMSIS FFT context before enabling USB/PDM interrupt sources.
	const H1FrontendStatus frontendInitStatus =
		h1InitializeFrontendM55(gM55FrontendRuntime, h1M55CompactMel());
	const bool frontendRuntimeReady =
		frontendInitStatus == H1FrontendStatus::Ok &&
		h1FrontendM55Ready(gM55FrontendRuntime);
	printk("H1_FRONTEND_INIT status=%u ready=%u\n",
	       unsigned(frontendInitStatus), unsigned(frontendRuntimeReady));

	const int usbStatus = h1UsbInit();
#if defined(H1_I2S3_ACQUISITION)
	h1AudioI2sInit();
#endif
	printk("H1_USB_INIT status=%d controller=DWC3 stack=USB_DEVICE_STACK_NEXT "
	       "class=CDC_ACM vid=2fe3 pid=0001 serial=H1DEV1 optional=1 wait_for_dtr=0\n",
	       usbStatus);
	/* Keep the init function linked in both A/B images while the build-time
	 * setting chooses whether it executes, preserving diagnostic layout. */
	volatile bool skipPdmInit = H1_DIAG_SKIP_PDM_INIT != 0;
#if !defined(H1_DIAG_SKIP_PDM_INIT) || H1_DIAG_SKIP_PDM_INIT == 0
	const int micInitStatus = skipPdmInit ? -ENOTSUP : h1AudioPdmInit();
#else
	const int micInitStatus = -ENOTSUP;
#endif
	printk("H1_MIC_INIT status=%d mechanism=ALIF_PDM_IRQ_K_MEM_SLAB dma=NONE "
	       "channel=4 format=PCM16_LE_MONO requested_rate_hz=32000 "
	       "clock=ALIF_PDM_76M8_CLK mode=5 blocks=%u block_bytes=%u "
	       "ring_samples=%u window_samples=%u stride_samples=%u "
	       "ring=%p selected_window=%p continuous_producer=1\n",
	       micInitStatus, unsigned(H1_MIC_SLAB_BLOCKS),
	       unsigned(H1_MIC_BLOCK_BYTES), unsigned(H1_MIC_RING_SAMPLES),
	       unsigned(H1_MIC_WINDOW_SAMPLES), unsigned(H1_MIC_STRIDE_SAMPLES),
	       h1PcmRingStorage(), h1SelectedPcmWindow());
	h1BaselineInit();
#if defined(H1_POSTPROCESSING_OBSERVATION)
	h1PostprocessObserverInit();
#endif
#if defined(H1_HOT_PATH_VALIDATION_OBSERVATION)
	h1ValidationObserverInit();
#endif
	gComputeReady = frontendRuntimeReady && reportNpuIdentity() &&
		h1FastGuardPrepare() && preparePersistentContexts();
	printk("H1_LIFECYCLE_INIT complete_before_waveform_ready=%u\n",
	       unsigned(h1LifecycleInitializationComplete));
	printk("H1_READY compute_ready=%u mic_ready=%u usb_optional=1 uart4_fallback=1 "
	       "uart_commands=C_compact,G_legacy_full sw4_normal=SE\n",
	       unsigned(gComputeReady), unsigned(micInitStatus == 0));

	const device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	for (;;) {
#if defined(H1_I2S3_ACQUISITION)
		h1AudioI2sService();
#endif
		H1UsbFrame frame{};
		const H1UsbPollResult poll = h1UsbPoll(frame);
		if (poll == H1UsbPollResult::Frame) {
			handleFrame(frame);
		} else if (poll == H1UsbPollResult::ProtocolError) {
			handleProtocolError(frame);
		}
		if (device_is_ready(console)) {
			unsigned char command = 0;
			if (uart_poll_in(console, &command) == 0) {
				handleUartFallback(command);
			}
		}
		if (poll == H1UsbPollResult::None) {
			k_sleep(K_MSEC(1));
		}
	}
}

extern "C" void k_sys_fatal_error_handler(unsigned int reason,
					 const struct arch_esf *esf)
{
	if (!h1M55SpectralDiagnosticValid(&gM55SpectralDiagnostic)) {
		h1M55SpectralDiagnosticInitialize(&gM55SpectralDiagnostic);
	}
	const uint32_t exceptionNumber = SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk;
	if (gM55FrontendExecution.state == H1M55FrontendState::Running) {
		gM55FrontendExecution.failedStage = gM55FrontendExecution.currentStage;
		gM55FrontendExecution.reason = reason;
		gM55FrontendExecution.state = H1M55FrontendState::Faulted;
	}
	h1M55SpectralDiagnosticFault(
		&gM55SpectralDiagnostic, reason, exceptionNumber,
		SCB->CFSR, SCB->HFSR, SCB->MMFAR, SCB->BFAR,
		esf ? esf->basic.pc : 0, esf ? esf->basic.lr : 0,
		esf ? esf->basic.xpsr : 0);
	k_fatal_halt(reason);
}
