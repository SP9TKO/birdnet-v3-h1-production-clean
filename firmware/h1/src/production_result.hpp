// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr uint32_t H1_PRODUCTION_CONTRACT_VERSION = 1;
constexpr uint32_t H1_PRODUCTION_CLASSES = 11560;
constexpr uint32_t H1_PRODUCTION_TOP_COUNT = 3;
constexpr size_t H1_PRODUCTION_WIRE_BYTES = 76;

struct H1ResultGenerationId {
	uint64_t epoch, inferenceSequence;
};
struct H1ResultInputGenerationId {
	uint64_t waveformOwnerEpoch, waveformGeneration;
	uint32_t producer;
};
struct H1ProductionTopEntry {
	uint32_t classIndex, scoreBits;
};
struct H1ProductionResult {
	uint32_t contractVersion, status, valid, topCount;
	H1ResultGenerationId resultGenerationId;
	H1ResultInputGenerationId inputGenerationId;
	H1ProductionTopEntry top3[H1_PRODUCTION_TOP_COUNT];
};
static_assert(sizeof(H1ResultGenerationId) == 16);
static_assert(sizeof(H1ProductionTopEntry) == 8);
static_assert(sizeof(H1ProductionResult) == 80);
static_assert(alignof(H1ProductionResult) == 8);
static_assert(offsetof(H1ProductionResult, resultGenerationId) == 16);
static_assert(offsetof(H1ProductionResult, inputGenerationId) == 32);
static_assert(offsetof(H1ProductionResult, top3) == 56);

inline bool h1ResultSame(H1ResultGenerationId a, H1ResultGenerationId b)
{
	return a.epoch == b.epoch && a.inferenceSequence == b.inferenceSequence;
}
inline uint64_t h1ResultReadLe64(const uint8_t *p)
{
	uint64_t value = 0;
	for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
	return value;
}
inline void h1ResultWriteLe(uint8_t *p, uint64_t value, unsigned bytes)
{
	for (unsigned i = 0; i < bytes; ++i) p[i] = uint8_t(value >> (8 * i));
}
inline void h1ResultEncode(const H1ProductionResult &r, uint8_t *p)
{
	h1ResultWriteLe(p, r.contractVersion, 4);
	h1ResultWriteLe(p+4, r.status, 4);
	h1ResultWriteLe(p+8, r.valid, 4);
	h1ResultWriteLe(p+12, r.topCount, 4);
	h1ResultWriteLe(p+16, r.resultGenerationId.epoch, 8);
	h1ResultWriteLe(p+24, r.resultGenerationId.inferenceSequence, 8);
	h1ResultWriteLe(p+32, r.inputGenerationId.waveformOwnerEpoch, 8);
	h1ResultWriteLe(p+40, r.inputGenerationId.waveformGeneration, 8);
	h1ResultWriteLe(p+48, r.inputGenerationId.producer, 4);
	for (unsigned i = 0; i < H1_PRODUCTION_TOP_COUNT; ++i) {
		h1ResultWriteLe(p+52+8*i, r.top3[i].classIndex, 4);
		h1ResultWriteLe(p+56+8*i, r.top3[i].scoreBits, 4);
	}
}

enum class H1ResultMode : uint32_t { LegacyDiagnostic, Production, PinnedDiagnostic };
enum class H1ResultSourceState : uint32_t { Invalid, Writing, Complete };
enum class H1ResultError : uint32_t {
	None, OwnerSessionMismatch, PinActive, GenerationExhausted, StaleGeneration,
	EvidenceNotRetained, IncompleteEvidence, BufferMismatch, InvalidResult,
};
inline const char *h1ResultErrorName(H1ResultError e)
{
	switch (e) {
	case H1ResultError::None: return "OK";
	case H1ResultError::OwnerSessionMismatch: return "OWNER_SESSION_MISMATCH";
	case H1ResultError::PinActive: return "DIAGNOSTIC_PIN_ACTIVE";
	case H1ResultError::GenerationExhausted: return "GENERATION_EXHAUSTED";
	case H1ResultError::StaleGeneration: return "STALE_GENERATION";
	case H1ResultError::EvidenceNotRetained: return "EVIDENCE_NOT_RETAINED";
	case H1ResultError::IncompleteEvidence: return "INCOMPLETE_EVIDENCE";
	case H1ResultError::BufferMismatch: return "RESULT_BUFFER_GENERATION_MISMATCH";
	case H1ResultError::InvalidResult: return "INVALID_RESULT";
	}
	return "INVALID_RESULT";
}
struct H1ResultSourceTag {
	H1ResultGenerationId generation;
	H1ResultSourceState state;
};

// Single application owner only. No interrupt reads or writes this publication.
// The nonce is a fresh host binding for the current CDC connection, not an
// imported result token. Reset/connection retirement requires a new handshake.
struct H1ResultService {
	uint64_t epoch, epochInverse, sequence, nonce[2], pinDeadline;
	uint32_t epochStamp, connection;
	bool initialized, bound, running, productionReady, evidenceComplete, pinned;
	H1ResultMode mode;
	H1ResultGenerationId admitted, evidence;
	H1ResultInputGenerationId input;
	H1ResultSourceTag sources[7];
	H1ProductionResult retained;

	bool initialize()
	{
		constexpr uint32_t stamp = UINT32_C(0x48525031);
		const bool retainedEpoch = epochStamp == stamp && epoch != 0 &&
			(epoch ^ epochInverse) == UINT64_MAX;
		const uint64_t previous = retainedEpoch ? epoch : 0;
		std::memset(this, 0, sizeof(*this));
		if (previous == UINT64_MAX) {
			// Preserve exhausted epoch authority across another initialization.
			epoch = previous;
			epochInverse = ~epoch;
			epochStamp = stamp;
			return false;
		}
		epoch = previous+1;
		epochInverse = ~epoch;
		epochStamp = stamp;
		initialized = true;
		return true;
	}
	void retireEvidence()
	{
		evidenceComplete = false;
		pinned = false;
		pinDeadline = 0;
		for (auto &s : sources) s.state = H1ResultSourceState::Invalid;
	}
	void expire(uint64_t now)
	{
		if (pinned && now >= pinDeadline) retireEvidence();
	}
	bool writerAllowed(uint64_t now)
	{
		expire(now);
		return initialized && !running && !pinned;
	}
	bool syncConnection(uint32_t current)
	{
		if (connection == current) return false;
		retireEvidence();
		bound = false;
		running = false;
		productionReady = false;
		connection = current;
		return true;
	}
	H1ResultError open(uint64_t a, uint64_t b, uint64_t now)
	{
		if (!writerAllowed(now)) return H1ResultError::PinActive;
		if ((a|b) == 0 || (nonce[0] == a && nonce[1] == b))
			return H1ResultError::OwnerSessionMismatch;
		if (epoch == UINT64_MAX) return H1ResultError::GenerationExhausted;
		retireEvidence();
		productionReady = false;
		++epoch;
		epochInverse = ~epoch;
		sequence = 0;
		nonce[0] = a;
		nonce[1] = b;
		bound = true;
		return H1ResultError::None;
	}
	H1ResultError authenticate(uint64_t a, uint64_t b, uint64_t requestedEpoch) const
	{
		return initialized && bound && a == nonce[0] && b == nonce[1] &&
			requestedEpoch == epoch ? H1ResultError::None : H1ResultError::OwnerSessionMismatch;
	}
	H1ResultError begin(H1ResultMode selected, H1ResultInputGenerationId captured, uint64_t now)
	{
		if (!writerAllowed(now)) return H1ResultError::PinActive;
		if (selected != H1ResultMode::LegacyDiagnostic && !bound) return H1ResultError::OwnerSessionMismatch;
		if (selected != H1ResultMode::LegacyDiagnostic &&
		    (captured.producer != 1 || !captured.waveformOwnerEpoch || !captured.waveformGeneration))
			return H1ResultError::InvalidResult;
		if (sequence == UINT64_MAX) return H1ResultError::GenerationExhausted;
		retireEvidence();
		productionReady = false;
		std::memset(&retained, 0, sizeof(retained));
		mode = selected;
		input = captured;
		admitted = {epoch, ++sequence};
		running = true;
		return H1ResultError::None;
	}
	bool sourceBegin(unsigned kind)
	{
		if (!running || kind >= 7 || pinned) return false;
		sources[kind] = {admitted, H1ResultSourceState::Writing};
		return true;
	}
	bool sourceComplete(unsigned kind)
	{
		if (!running || kind >= 7 || sources[kind].state != H1ResultSourceState::Writing ||
		    !h1ResultSame(sources[kind].generation, admitted)) return false;
		sources[kind].state = H1ResultSourceState::Complete;
		return true;
	}
	bool coherent() const
	{
		for (const auto &s : sources)
			if (s.state != H1ResultSourceState::Complete || !h1ResultSame(s.generation, admitted))
				return false;
		return true;
	}
	H1ResultError diagnosticComplete()
	{
		if (mode == H1ResultMode::Production) return H1ResultError::EvidenceNotRetained;
		if (!running || !coherent()) return H1ResultError::IncompleteEvidence;
		evidence = admitted;
		evidenceComplete = true;
		running = false;
		return H1ResultError::None;
	}
	H1ResultError pin(H1ResultGenerationId token, uint64_t now, uint64_t duration)
	{
		expire(now);
		if (mode == H1ResultMode::Production) return H1ResultError::EvidenceNotRetained;
		if (!h1ResultSame(token, evidence) || !h1ResultSame(token, admitted)) return H1ResultError::StaleGeneration;
		if (!evidenceComplete || running) return H1ResultError::IncompleteEvidence;
		if (!coherent()) return H1ResultError::BufferMismatch;
		if (duration == 0 || duration > UINT64_MAX-now) return H1ResultError::IncompleteEvidence;
		pinDeadline = now+duration;
		pinned = true;
		return H1ResultError::None;
	}
	H1ResultError checkEvidence(H1ResultGenerationId token, uint64_t now)
	{
		expire(now);
		if (mode == H1ResultMode::Production) return H1ResultError::EvidenceNotRetained;
		if (!h1ResultSame(token, evidence) || !h1ResultSame(token, admitted)) return H1ResultError::StaleGeneration;
		if (!evidenceComplete || !pinned || running) return H1ResultError::IncompleteEvidence;
		return coherent() ? H1ResultError::None : H1ResultError::BufferMismatch;
	}
	void abort()
	{
		running = false;
		retireEvidence();
	}
	H1ResultError publish(uint32_t status, const H1ProductionTopEntry *top, uint32_t finiteCount)
	{
		if (!running || mode != H1ResultMode::Production || status == 0 || status == 8 || status > 9)
			return H1ResultError::InvalidResult;
		H1ProductionResult next{};
		next.contractVersion = H1_PRODUCTION_CONTRACT_VERSION;
		next.status = status;
		next.resultGenerationId = admitted;
		next.inputGenerationId = input;
		if (status == 1) {
			if (!coherent() || !top || finiteCount != H1_PRODUCTION_CLASSES ||
			    input.producer != 1 || input.waveformOwnerEpoch == 0 || input.waveformGeneration == 0)
				return H1ResultError::InvalidResult;
			for (unsigned i = 0; i < H1_PRODUCTION_TOP_COUNT; ++i) {
				float score;
				std::memcpy(&score, &top[i].scoreBits, sizeof(score));
				if (top[i].classIndex >= H1_PRODUCTION_CLASSES || !std::isfinite(score))
					return H1ResultError::InvalidResult;
				for (unsigned j = 0; j < i; ++j)
					if (top[i].classIndex == top[j].classIndex) return H1ResultError::InvalidResult;
				if (i) {
					float previous;
					std::memcpy(&previous, &top[i-1].scoreBits, sizeof(previous));
					if (previous < score || (previous == score && top[i-1].classIndex > top[i].classIndex))
						return H1ResultError::InvalidResult;
				}
				next.top3[i] = top[i];
			}
			next.valid = 1;
			next.topCount = H1_PRODUCTION_TOP_COUNT;
		}
		// One compact slot, never H1RunResult. Readers run on this same owner.
		std::memcpy(&retained, &next, sizeof(retained));
		__atomic_thread_fence(__ATOMIC_RELEASE);
		running = false;
		productionReady = true;
		return H1ResultError::None;
	}
	H1ResultError checkResult(H1ResultGenerationId token) const
	{
		return productionReady && !running && h1ResultSame(token, admitted) &&
			h1ResultSame(token, retained.resultGenerationId) ? H1ResultError::None : H1ResultError::StaleGeneration;
	}
};

struct H1ProductionOperations {
	uint32_t resultEvidenceScans, scoreEvidenceScans, repeatComparisons, diagnosticSaves;
	uint32_t frontendInternalScans, frontendInternalBytes, generatedScores, finiteChecks;
};
struct H1ProductionProof {
	H1ResultGenerationId generation;
	H1ResultInputGenerationId input;
	uint64_t primaryStart, ready, publicationStart, publicationEnd;
	uint64_t frontendStart, frontendEnd, backboneStart, backboneEnd;
	uint64_t gemStart, gemEnd, classifierStart, classifierEnd;
	uint64_t scoreStart, scoreEnd, topStart, topEnd;
	H1ProductionOperations operations;
	uint32_t observed, clockHz, status, admitted;
};
struct H1ProductionRuntime {
	H1ResultService service;
	H1ProductionProof proof;
};
static_assert(sizeof(H1ProductionRuntime) <= 4096);
