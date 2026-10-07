// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Only the serialized application command owner may call this interface.
// Tokens are internal capabilities; no transport command imports them.
enum class H1WaveformState : uint32_t { Empty, Writing, Unvalidated, Valid, Invalid };
enum class H1WaveformProducer : uint32_t { None, Upload, Pdm };

struct H1WaveformToken {
	uint64_t epoch;
	uint64_t generation;
	uintptr_t owner;
};

struct H1WaveformReceipt {
	uint64_t epoch;
	uint64_t generation;
	const float *base;
	uint32_t samples, bytes, dtype, sampleRate, channels;
	H1WaveformProducer producer;
	uint32_t validatedCrc, rawCrc, finiteCount, canonical;
	uint32_t validationFlags, complete, error;
	char declaredSha256[65];
};

constexpr uint32_t H1_WAVEFORM_UPLOAD_VALIDATION_FLAGS = 0x1fu;
constexpr bool LIVE_AUDIO_EXPECTED_CONTENT_CRC_REQUIRED = false;

struct H1WaveformCounters {
	uint64_t uploadGenerationsCreated, uploadGenerationsValidated;
	uint64_t validGenerations, invalidGenerations;
	uint64_t rawCrcScans, rawCrcBytes, residentCrcScans, residentCrcBytes;
	uint64_t inferenceCrcScans, inferenceCrcBytes;
	uint64_t inferenceLeases, ownershipFailures;
	uint64_t invalidGenerationInferences, staleWaveformRejections;
};

struct H1WaveformInferenceProof {
	uint64_t epoch, generation, acquireCycles, releaseCycles;
	uint32_t crcScans, crcBytes, shaScans, identityScans, valid;
};

struct H1WaveformLifecycle {
	uint64_t epoch, epochInverse, counter;
	uint32_t epochStamp, state;
	H1WaveformProducer producer;
	const float *base;
	uint32_t samples, bytes;
	uint32_t mutationOwned, writerActive, leaseActive, parserActive, parserDenied;
	H1WaveformToken parserToken;
	H1WaveformReceipt receipt;
	H1WaveformCounters counters;
	H1WaveformInferenceProof lastInference;
	H1WaveformInferenceProof diagnostic[20];
	uint32_t transitions[8], transitionCount;

	bool initialize();
	bool begin(H1WaveformProducer source, const float *resident, uint32_t count,
		   uint32_t length, H1WaveformToken &token);
	bool quiesce(const H1WaveformToken &token);
	bool resumeWrite(const H1WaveformToken &token);
	bool canWrite(const H1WaveformToken &token, const float *resident) const;
	bool publish(const H1WaveformToken &token, const H1WaveformReceipt &validated);
	void invalidate(const H1WaveformToken &token);
	bool acquire(H1WaveformProducer source, const float *resident, uint32_t count,
		     uint32_t length, H1WaveformToken &token, H1WaveformReceipt &snapshot,
		     bool inference = true);
	bool release(const H1WaveformToken &token, bool inference = true);
	void noteRawCrc(uint32_t length);
	void noteResidentCrc(uint32_t length, bool inference = false);

private:
	bool matches(const H1WaveformToken &token) const;
	void transition(H1WaveformState next);
	void retire();
};

extern H1WaveformLifecycle h1WaveformLifecycle;

// Parser callbacks run on the same application owner as validation/inference.
bool h1WaveformUploadAttemptBegin();
void h1WaveformUploadAttemptAbort();

class H1WaveformInputLease {
public:
	H1WaveformToken token{};
	H1WaveformReceipt snapshot{};
	bool active = false;
	bool inference = true;

	~H1WaveformInputLease() { if (active) release(); }
	bool acquire(H1WaveformProducer source, const float *base, uint32_t samples,
		     uint32_t bytes, bool measured = true)
	{
		inference = measured;
		active = h1WaveformLifecycle.acquire(source, base, samples, bytes,
						     token, snapshot, inference);
		return active;
	}
	bool release()
	{
		if (!active) return false;
		active = false;
		return h1WaveformLifecycle.release(token, inference);
	}
	H1WaveformInputLease() = default;
	H1WaveformInputLease(const H1WaveformInputLease &) = delete;
	H1WaveformInputLease &operator=(const H1WaveformInputLease &) = delete;
};
