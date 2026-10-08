// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "production_result.hpp"
#include "runtime_profile.h"
#include <cstddef>
#include <cstdint>

// Frozen V1 marker order, record geometry and dedicated ownership.
enum class H1FFixed : uint32_t {
	F0_BEGIN = 0,
	F0_END = 1,
	READY_DONE = 2,
	COMPUTE_BEGIN = 3,
	COMPUTE_END = 4,
	DB_PARENT_BEGIN = 5,
	DB_PARENT_END = 6,
	dbStart = 7,
	dbEnd = 8,
	CROP_PARENT_BEGIN = 9,
	CROP_PARENT_END = 10,
	CROP_MATH_BEGIN = 11,
	CROP_MATH_END = 12,
	NORMALIZE_BEGIN = 13,
	NORMALIZE_END = 14,
	RESIZE_PARENT_BEGIN = 15,
	RESIZE_PARENT_END = 16,
	resizeStart = 17,
	resizeEnd = 18,
	LAYOUT_PARENT_BEGIN = 19,
	LAYOUT_PARENT_END = 20,
	layoutStart = 21,
	layoutEnd = 22,
	FINITE_BEGIN = 23,
	FINITE_END = 24,
	CRC_BEGIN = 25,
	CRC_END = 26,
	P0 = 27,
	P1 = 28,
	frontendStart = 29,
	frontendEnd = 30,
	backboneStart = 31,
	backboneEnd = 32,
	gemStart = 33,
	gemEnd = 34,
	classifierStart = 35,
	classifierEnd = 36,
	scoreStart = 37,
	scoreEnd = 38,
	topStart = 39,
	topEnd = 40,
	publicationStart = 41,
	publicationEnd = 42,
	backboneQuantizeStart = 43,
	backboneQuantizeEnd = 44,
	classifierQuantizeStart = 45,
	classifierQuantizeEnd = 46,
	postprocessStart = 47,
	postprocessEnd = 48,
	Count = 49
};
enum class H1FFrame : uint32_t {
	C_BEGIN = 0,
	C_END = 1,
	frameStart = 2,
	frameEnd = 3,
	hannStart = 4,
	hannEnd = 5,
	cfftStart = 6,
	cfftEnd = 7,
	splitStart = 8,
	splitEnd = 9,
	squaresStart = 10,
	squaresEnd = 11,
	Count = 12
};
enum class H1FGroup : uint32_t {
	G_SPEC_BEGIN = 0,
	G_SPEC_END = 1,
	G_MEL_BEGIN = 2,
	G_MEL_END = 3,
	M_CALL_BEGIN = 4,
	M_CALL_END = 5,
	M_MATH_BEGIN = 6,
	M_MATH_END = 7,
	M_FINITE_0_BEGIN = 8,
	M_FINITE_0_END = 9,
	M_FINITE_1_BEGIN = 10,
	M_FINITE_1_END = 11,
	M_FINITE_2_BEGIN = 12,
	M_FINITE_2_END = 13,
	M_FINITE_3_BEGIN = 14,
	M_FINITE_3_END = 15,
	Count = 16
};
enum class H1FCounter : uint32_t {
	FRONTEND_CALLS = 0,
	SPECTRAL_GROUPS = 1,
	SPECTRAL_FRAMES = 2,
	FRAME_PREPARATIONS = 3,
	HANN_OPERATIONS = 4,
	CFFT_OPERATIONS = 5,
	REAL_SPLIT_OPERATIONS = 6,
	SCALAR_POWER_OPERATIONS = 7,
	CMSIS_POWER_OPERATIONS = 8,
	HYPOT_POWER_OPERATIONS = 9,
	MEL_GROUPS = 10,
	MEL_MVE_GROUPS = 11,
	MEL_SCALAR_GROUPS = 12,
	MEL_FINITE_CALLS = 13,
	MEL_FINITE_ELEMENTS = 14,
	MEL_CAPTURE_CALLS = 15,
	MEL_CAPTURE_BYTES = 16,
	LOG_CALLS = 17,
	CROP_ELEMENTS = 18,
	NORMALIZE_ELEMENTS = 19,
	RESIZE_PIXELS = 20,
	LAYOUT_ELEMENTS = 21,
	FRONTEND_OUTPUT_FINITE_CALLS = 22,
	FRONTEND_OUTPUT_FINITE_ELEMENTS = 23,
	FRONTEND_INTERNAL_CRC_CALLS = 24,
	FRONTEND_INTERNAL_CRC_BYTES = 25,
	RESULT_EVIDENCE_SCANS = 26,
	SCORE_EVIDENCE_SCANS = 27,
	DIAGNOSTIC_REPEAT_COMPARISONS = 28,
	DIAGNOSTIC_SAVES = 29,
	GENERATED_SCORES = 30,
	SCORE_FINITE_CHECKS = 31,
	PRODUCTION_TOP_COUNT = 32,
	ACCEPTED_SELECTOR_COUNT = 33,
	U85_BACKBONE_COMMANDS = 34,
	U85_CLASSIFIER_COMMANDS = 35,
	U85_BACKBONE_COMPLETIONS = 36,
	U85_CLASSIFIER_COMPLETIONS = 37,
	clock_read_count = 38,
	timestamp_store_count = 39,
	duplicate_marker_count = 40,
	missing_marker_count = 41,
	observer_bytes_written = 42,
	counter_overflow = 43,
	record_overflow = 44,
	unexpected_backend_count = 45,
	fault_count = 46,
	guard_failure_count = 47,
	Count = 48
};

constexpr uint32_t H1_F_MAGIC = UINT32_C(0x314f4648);
constexpr uint32_t H1_F_VERSION = 1;
constexpr uint32_t H1_F_CLOCK_HZ = 400000000;
constexpr uint32_t H1_F_RECORD_BYTES = 32768;
constexpr uint32_t H1_F_PAYLOAD_BYTES = 25472;
constexpr uint32_t H1_F_TIMESTAMP_BITS = 3072;
enum class H1FKind : uint32_t { None, IsolatedProduction, Campaign };
enum class H1FError : uint32_t { None, Bounds, DuplicateMarker, CounterOverflow,
 MissingMarker, UnexpectedMarker, Clock, Backend, Counts, Guard, Runtime,
 IncompleteProduction, Held, StaleCampaign, Authentication, Exhausted, AlreadyRun };
enum class H1FState : uint32_t { Empty, Capturing, Complete, Failed };

struct H1FMetadata {
 uint32_t magic, version, ordinal, kind, state, error, clockHz, payloadBytes;
 uint32_t finiteCount, outputCrc32, frameCount, groupCount, powerStage, weightCount, resultStatus, inputProducer;
 uint64_t campaignId, nonce0, nonce1, ownerEpoch, inferenceSequence, inputEpoch, inputGeneration, runSequence;
 uint32_t counters[48];
 uint64_t accounting[16]; // [0] unchanged native total; [1..] bounded runtime proof
 uint8_t reservedTail[64];
};
struct alignas(32) H1FRecord {
 H1FMetadata metadata;
 uint64_t fixed[64];
 uint64_t frames[188][12];
 uint64_t groups[47][16];
 uint8_t presence[384];
 uint8_t padding[7296];
};
struct H1FCalibration { uint64_t begin, captured, end; };
struct H1FControl {
 uint32_t magic, version, running, held, error, kind, requested, stored;
 uint32_t warmups, measured, collectOrdinal, collectOffset, campaignIssued, clockHz;
 uint32_t irqBefore, irqAfter, calibrationCount, calibrationStores, calibrationBytes;
 uint32_t calibrationIrqBefore, calibrationIrqAfter, cacheSelector, cacheControl;
 uint32_t finalFaults, finalLeaseValid;
 uint64_t campaignId, nextCampaignId, nonce0, nonce1, ownerEpoch, inputEpoch, inputGeneration;
 H1FRecord *active;
 H1FCalibration calibration[20];
 uint8_t calibrationPresence[3];
 char sourceBundle[65], planSha256[65];
};
struct H1FHeader { H1FControl control; uint8_t reserved[4096-sizeof(H1FControl)]; };
struct alignas(32) H1FStorage {
 uint8_t prefixGuard[32];
 H1FHeader header;
 H1FRecord records[25];
 uint8_t suffixGuard[32];
};
static_assert(sizeof(H1FMetadata)==512 && offsetof(H1FMetadata,counters)==128 && offsetof(H1FMetadata,accounting)==320);
static_assert(sizeof(H1FRecord)==32768 && alignof(H1FRecord)==32);
static_assert(offsetof(H1FRecord,fixed)==512 && offsetof(H1FRecord,frames)==1024);
static_assert(offsetof(H1FRecord,groups)==19072 && offsetof(H1FRecord,presence)==25088 && offsetof(H1FRecord,padding)==25472);
static_assert(sizeof(H1FHeader)==4096 && offsetof(H1FStorage,header)==32);
static_assert(offsetof(H1FStorage,records)==4128 && offsetof(H1FStorage,suffixGuard)==823328 && sizeof(H1FStorage)==823360);
static_assert(uint32_t(H1FFixed::Count)==49 && uint32_t(H1FFrame::Count)==12 && uint32_t(H1FGroup::Count)==16 && uint32_t(H1FCounter::Count)==48);
extern "C" H1FStorage h1FrontendObserverStorage;

bool h1FInit();
bool h1FGuards();
bool h1FWriterAllowed();
bool h1FActive();
uint64_t h1FRead(); // Read only; capture accounting occurs after the corresponding end.
void h1FFixedAt(H1FFixed marker,uint64_t value,uint32_t addedReads=0);
void h1FFixedPair(H1FFixed begin,H1FFixed end,uint64_t a,uint64_t b,uint32_t addedReads=2);
void h1FFramePair(uint32_t frame,H1FFrame begin,uint64_t a,uint64_t b,H1FCounter operation,uint32_t addedReads=0);
void h1FCallPair(uint32_t frame,uint64_t a,uint64_t b,bool completed);
void h1FGroupPair(uint32_t group,H1FGroup begin,uint64_t a,uint64_t b);
void h1FCount(H1FCounter counter,uint32_t count=1);
void h1FBackend(bool cmsisPower,bool captureStages,uint32_t weights);
void h1FMelCompleted(bool usedMve);
void h1FNativeResult(uint64_t total,uint32_t finite,uint32_t crc);
H1FError h1FBeginSet(H1FKind kind,uint64_t nonce0,uint64_t nonce1,uint64_t epoch,uint64_t inputEpoch,uint64_t inputGeneration);
bool h1FBeginRecord(uint32_t ordinal);
void h1FStopRecord(H1FError error);
void h1FFinishRecord(const H1ProductionProof &proof,const H1RuntimeProfile &profile,uint32_t selected,uint32_t productTopCount,uint32_t runSequence,uint32_t faults,
                    uint32_t leaseValid,uint32_t waveformCrc,uint32_t waveformSha,uint32_t waveformIdentity);
void h1FEndSet(uint32_t irqAfter);
void h1FCalibrate(uint32_t irqBefore,uint32_t selector,uint32_t control);
H1FError h1FCheckOwner(uint64_t nonce0,uint64_t nonce1,uint64_t epoch,uint64_t campaign,bool discover=false);
H1FError h1FReadRange(uint32_t ordinal,uint32_t offset,uint32_t count,const uint8_t *&bytes);
void h1FExported(uint32_t ordinal,uint32_t offset,uint32_t count,bool sent);
