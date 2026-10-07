// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#include "waveform_lifecycle.hpp"
#include <cstring>
#include <limits>

H1WaveformLifecycle h1WaveformLifecycle
	__attribute__((section(".h1_waveform_lifecycle"), aligned(32)));
static_assert(sizeof(H1WaveformLifecycle) <= 4096, "bounded waveform metadata");

namespace {
constexpr uint32_t kEpochStamp = 0x57474531u;
constexpr uint32_t kSamples = 96000, kBytes = 384000;
}

bool H1WaveformLifecycle::initialize()
{
	// Only the integrity-tagged epoch may survive reset. Receipts, tokens,
	// counters and owners are always discarded, regardless of resident bytes.
	const bool retained = epochStamp == kEpochStamp && epoch != 0 &&
		epochInverse == ~epoch;
	const uint64_t next = retained ?
		(epoch == UINT64_MAX ? 0 : epoch + 1) : 1;
	std::memset(this, 0, sizeof(*this));
	epoch = next;
	epochInverse = ~next;
	epochStamp = kEpochStamp;
	transition(H1WaveformState::Empty);
	return next != 0;
}

bool H1WaveformLifecycle::matches(const H1WaveformToken &token) const
{
	return epoch != 0 && token.owner == reinterpret_cast<uintptr_t>(this) &&
		token.epoch == epoch && token.generation != 0 && token.generation == counter;
}

void H1WaveformLifecycle::transition(H1WaveformState next)
{
	if (transitionCount < 8) transitions[transitionCount++] = uint32_t(next);
	// Receipt publication is ordered before this final atomic state store.
	__atomic_store_n(&state, uint32_t(next), __ATOMIC_RELEASE);
}

void H1WaveformLifecycle::retire()
{
	std::memset(&receipt, 0, sizeof(receipt));
	mutationOwned = writerActive = 0;
	if (state != uint32_t(H1WaveformState::Invalid)) ++counters.invalidGenerations;
	transition(H1WaveformState::Invalid);
}

bool H1WaveformLifecycle::begin(H1WaveformProducer source, const float *resident,
	uint32_t count, uint32_t length, H1WaveformToken &token)
{
	token = {};
	if (leaseActive || mutationOwned || source == H1WaveformProducer::None ||
	    !resident || count != kSamples || length != kBytes) {
		++counters.staleWaveformRejections;
		return false;
	}
	// An exhausted attempt cannot leave an old VALID generation usable.
	if (epoch == 0 || counter == UINT64_MAX) {
		retire();
		return false;
	}
	std::memset(&receipt, 0, sizeof(receipt));
	transitionCount = 0;
	transitions[transitionCount++] = state;
	++counter;
	producer = source;
	base = resident; samples = count; bytes = length;
	mutationOwned = writerActive = 1;
	token = {epoch, counter, reinterpret_cast<uintptr_t>(this)};
	if (source == H1WaveformProducer::Upload) ++counters.uploadGenerationsCreated;
	transition(H1WaveformState::Writing);
	return true;
}

bool H1WaveformLifecycle::canWrite(const H1WaveformToken &token,
	const float *resident) const
{
	return matches(token) && mutationOwned && writerActive && !leaseActive &&
		state == uint32_t(H1WaveformState::Writing) && base == resident;
}

bool H1WaveformLifecycle::quiesce(const H1WaveformToken &token)
{
	if (!canWrite(token, base)) return false;
	writerActive = 0;
	transition(H1WaveformState::Unvalidated);
	return true;
}

bool H1WaveformLifecycle::resumeWrite(const H1WaveformToken &token)
{
	if (!matches(token) || !mutationOwned || writerActive || leaseActive ||
	    state != uint32_t(H1WaveformState::Unvalidated)) return false;
	writerActive = 1;
	transition(H1WaveformState::Writing);
	return true;
}

bool H1WaveformLifecycle::publish(const H1WaveformToken &token,
	const H1WaveformReceipt &validated)
{
	if (!matches(token) || !mutationOwned || writerActive || leaseActive ||
	    state != uint32_t(H1WaveformState::Unvalidated) ||
	    validated.epoch != epoch || validated.generation != counter ||
	    validated.producer != producer || validated.base != base ||
	    validated.samples != samples || validated.bytes != bytes ||
	    validated.dtype != 1 || validated.sampleRate != 32000 ||
	    validated.channels != 1 || !validated.complete || validated.error ||
	    validated.finiteCount != samples || validated.declaredSha256[64] != 0 ||
	    (producer == H1WaveformProducer::Upload &&
	     (validated.validationFlags != H1_WAVEFORM_UPLOAD_VALIDATION_FLAGS ||
	      validated.rawCrc != validated.validatedCrc))) {
		invalidate(token);
		return false;
	}
	receipt = validated;
	mutationOwned = 0;
	++counters.validGenerations;
	if (producer == H1WaveformProducer::Upload) ++counters.uploadGenerationsValidated;
	transition(H1WaveformState::Valid);
	return true;
}

void H1WaveformLifecycle::invalidate(const H1WaveformToken &token)
{
	if (!matches(token)) { ++counters.staleWaveformRejections; return; }
	// No ordinary writer may revoke an active lease. A detected violation
	// fails closed; it never republishes a receipt or mutates waveform bytes.
	if (leaseActive) ++counters.ownershipFailures;
	retire();
}

bool H1WaveformLifecycle::acquire(H1WaveformProducer source,
	const float *resident, uint32_t count, uint32_t length,
	H1WaveformToken &token, H1WaveformReceipt &snapshot, bool inference)
{
	token = {}; snapshot = {};
	if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != uint32_t(H1WaveformState::Valid) ||
	    writerActive || mutationOwned || leaseActive || epoch == 0 ||
	    producer != source || receipt.producer != source ||
	    base != resident || receipt.base != resident ||
	    count != samples || count != receipt.samples || count != kSamples ||
	    length != bytes || length != receipt.bytes || length != kBytes ||
	    receipt.epoch != epoch || receipt.generation != counter ||
	    receipt.dtype != 1 || receipt.sampleRate != 32000 || receipt.channels != 1 ||
	    !receipt.complete || receipt.error) {
		++counters.staleWaveformRejections;
		return false;
	}
	leaseActive = 1;
	token = {epoch, counter, reinterpret_cast<uintptr_t>(this)};
	snapshot = receipt;
	if (inference) {
		++counters.inferenceLeases;
		lastInference = {};
		lastInference.epoch = epoch;
		lastInference.generation = counter;
	}
	return true;
}

bool H1WaveformLifecycle::release(const H1WaveformToken &token, bool inference)
{
	const bool valid = matches(token) && leaseActive && !writerActive &&
		!mutationOwned && state == uint32_t(H1WaveformState::Valid) &&
		receipt.epoch == epoch && receipt.generation == counter &&
		receipt.producer == producer && receipt.base == base &&
		receipt.samples == samples && receipt.bytes == bytes;
	if (!valid) {
		++counters.ownershipFailures;
		retire();
	}
	leaseActive = 0;
	if (inference) lastInference.valid = valid;
	return valid;
}

void H1WaveformLifecycle::noteRawCrc(uint32_t length)
{
	++counters.rawCrcScans; counters.rawCrcBytes += length;
}

void H1WaveformLifecycle::noteResidentCrc(uint32_t length, bool inference)
{
	++counters.residentCrcScans; counters.residentCrcBytes += length;
	if (inference) {
		++counters.inferenceCrcScans; counters.inferenceCrcBytes += length;
		++lastInference.crcScans; lastInference.crcBytes += length;
	}
}
