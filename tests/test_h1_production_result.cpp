// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
// Host: c++ -std=c++17 -Wall -Wextra -Werror -O2 -Ifirmware/h1/src
//       tests/test_h1_production_result.cpp -o /tmp/h1-result-test
#include "production_result.hpp"
#include "topk_heap.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

static uint32_t bits(float value)
{
	uint32_t b;
	std::memcpy(&b, &value, sizeof(b));
	return b;
}
static H1ResultInputGenerationId input{7, 41, 1};
static H1ProductionTopEntry top[]{{3,bits(0.8f)},{4,bits(0.8f)},{8,bits(0.3f)}};
static void completeSources(H1ResultService &s)
{
	for (unsigned i = 0; i < 7; ++i) {
		assert(s.sourceBegin(i));
		assert(s.sourceComplete(i));
	}
}
static H1ResultService bound()
{
	H1ResultService s{};
	assert(s.initialize());
	assert(s.syncConnection(1));
	assert(s.open(101, 202, 0) == H1ResultError::None);
	assert(s.authenticate(101, 202, s.epoch) == H1ResultError::None);
	return s;
}
static void selectorPrefix()
{
	std::mt19937 random(42);
	std::vector<float> scores(H1_PRODUCTION_CLASSES);
	std::vector<uint32_t> expected(H1_PRODUCTION_CLASSES);
	std::iota(expected.begin(), expected.end(), 0);
	for (unsigned trial = 0; trial < 30; ++trial) {
		for (float &v : scores) v = trial == 0 ? 0.0f : float(random()%201)/200.0f;
		const auto better = [&](uint32_t a, uint32_t b) {
			return scores[a] > scores[b] || (scores[a] == scores[b] && a < b);
		};
		std::sort(expected.begin(), expected.end(), better);
		uint32_t selected[100];
		H1TopkHeapCounts counts{};
		const auto compare = [](const float *p, uint32_t a, uint32_t b) {
			return p[a] > p[b] || (p[a] == p[b] && a < b);
		};
		assert(h1SelectTopkHeap<false>(scores.data(), scores.size(), selected, 100, compare, counts) == 100);
		assert(std::equal(selected, selected+100, expected.begin()));
		auto s = bound();
		assert(s.begin(H1ResultMode::Production, input, 1) == H1ResultError::None);
		completeSources(s);
		H1ProductionTopEntry prefix[3];
		for (unsigned i = 0; i < 3; ++i) prefix[i] = {selected[i],bits(scores[selected[i]])};
		assert(s.publish(1, prefix, scores.size()) == H1ResultError::None);
		assert(std::memcmp(prefix, s.retained.top3, sizeof(prefix)) == 0);
	}
}
int main()
{
	selectorPrefix();
	auto s = bound();
	assert(s.open(101,202,0) == H1ResultError::OwnerSessionMismatch);
	assert(s.authenticate(102,202,s.epoch) == H1ResultError::OwnerSessionMismatch);
	assert(s.begin(H1ResultMode::Production, input, 1) == H1ResultError::None);
	const auto first = s.admitted;
	completeSources(s);
	assert(s.publish(1, top, 11560) == H1ResultError::None);
	assert(s.retained.valid == 1 && s.retained.topCount == 3 && s.retained.status == 1);
	assert(h1ResultSame(first,s.retained.resultGenerationId));
	assert(s.retained.inputGenerationId.waveformOwnerEpoch == 7 && s.retained.inputGenerationId.waveformGeneration == 41);
	assert(s.admitted.inferenceSequence != input.waveformGeneration);
	std::array<uint8_t,76> wire{};
	h1ResultEncode(s.retained,wire.data());
	assert(wire[0] == 1 && wire[4] == 1 && wire[8] == 1 && wire[12] == 3);
	assert(h1ResultReadLe64(wire.data()+16) == s.epoch);
	assert(h1ResultReadLe64(wire.data()+24) == first.inferenceSequence);
	assert(wire[52] == 3 && wire[60] == 4 && wire[68] == 8);
	assert(s.pin(first,2,10) == H1ResultError::EvidenceNotRetained);
	assert(s.checkEvidence(first,2) == H1ResultError::EvidenceNotRetained);
	assert(s.checkResult(first) == H1ResultError::None);
	for (uint32_t failure : {2u,3u,4u,5u,6u,7u,9u}) {
		assert(s.begin(H1ResultMode::Production,input,3) == H1ResultError::None);
		assert(s.checkResult(first) == H1ResultError::StaleGeneration);
		assert(!s.productionReady && !s.retained.valid);
		const auto admitted = s.admitted;
		assert(s.publish(failure,nullptr,0) == H1ResultError::None);
		assert(s.retained.status == failure && !s.retained.valid && s.retained.topCount == 0);
		assert(h1ResultSame(s.retained.resultGenerationId,admitted));
		assert(s.retained.inputGenerationId.waveformGeneration == input.waveformGeneration);
		for (const auto &t : s.retained.top3) assert(!t.classIndex && !t.scoreBits);
	}
	assert(s.admitted.inferenceSequence == 8);
	assert(s.begin(H1ResultMode::Production,input,4) == H1ResultError::None);
	completeSources(s);
	assert(s.publish(1,top,11559) == H1ResultError::InvalidResult);
	H1ProductionTopEntry wrong[3];
	std::memcpy(wrong,top,sizeof(wrong));
	wrong[2].classIndex = 11560;
	assert(s.publish(1,wrong,11560) == H1ResultError::InvalidResult);
	wrong[2] = top[0];
	assert(s.publish(1,wrong,11560) == H1ResultError::InvalidResult);
	std::memcpy(wrong,top,sizeof(wrong));
	wrong[1].scoreBits = bits(std::numeric_limits<float>::infinity());
	assert(s.publish(1,wrong,11560) == H1ResultError::InvalidResult);
	wrong[1].scoreBits = bits(std::numeric_limits<float>::quiet_NaN());
	assert(s.publish(1,wrong,11560) == H1ResultError::InvalidResult);
	std::memcpy(wrong,top,sizeof(wrong));
	std::swap(wrong[0],wrong[1]);
	assert(s.publish(1,wrong,11560) == H1ResultError::InvalidResult);
	assert(s.publish(8,nullptr,0) == H1ResultError::InvalidResult);
	assert(!s.productionReady);
	assert(s.publish(7,nullptr,0) == H1ResultError::None);
	assert(!s.retained.valid);
	const uint64_t seq = s.sequence;
	assert(s.begin(H1ResultMode::Production,{0,0,0},5) == H1ResultError::InvalidResult);
	assert(s.sequence == seq);
	assert(s.begin(H1ResultMode::PinnedDiagnostic,input,5) == H1ResultError::None);
	assert(s.sourceBegin(0));
	assert(s.diagnosticComplete() == H1ResultError::IncompleteEvidence);
	s.abort();
	assert(!s.evidenceComplete && !s.pinned);
	assert(s.begin(H1ResultMode::PinnedDiagnostic,input,6) == H1ResultError::None);
	completeSources(s);
	assert(s.diagnosticComplete() == H1ResultError::None);
	const auto diag = s.admitted;
	assert(s.pin(diag,10,100) == H1ResultError::None);
	assert(s.checkEvidence(diag,11) == H1ResultError::None);
	const uint64_t beforeRejected = s.sequence;
	assert(s.begin(H1ResultMode::Production,input,11) == H1ResultError::PinActive);
	assert(s.sequence == beforeRejected && !s.sourceBegin(0));
	assert(!s.writerAllowed(109));
	s.sources[2].generation.inferenceSequence++;
	assert(s.checkEvidence(diag,12) == H1ResultError::BufferMismatch);
	s.sources[2].generation = diag;
	s.sources[2].state = H1ResultSourceState::Writing;
	assert(s.checkEvidence(diag,13) == H1ResultError::BufferMismatch);
	s.sources[2].state = H1ResultSourceState::Complete;
	assert(s.checkEvidence({diag.epoch,diag.inferenceSequence-1},14) == H1ResultError::StaleGeneration);
	assert(s.writerAllowed(110));
	assert(!s.evidenceComplete && s.checkEvidence(diag,111) == H1ResultError::IncompleteEvidence);
	const auto oldEpoch = s.epoch;
	assert(s.open(303,404,120) == H1ResultError::None);
	assert(s.epoch == oldEpoch+1 && s.sequence == 0);
	assert(s.authenticate(101,202,oldEpoch) == H1ResultError::OwnerSessionMismatch);
	assert(s.begin(H1ResultMode::Production,input,121) == H1ResultError::None);
	assert(s.admitted.inferenceSequence == 1);
	s.abort();
	assert(s.syncConnection(2));
	assert(!s.bound && !s.productionReady && !s.pinned);
	assert(s.begin(H1ResultMode::Production,input,122) == H1ResultError::OwnerSessionMismatch);
	assert(s.initialize());
	assert(!s.bound && !s.sequence && !s.evidenceComplete && !s.productionReady);
	assert(s.authenticate(303,404,oldEpoch+1) == H1ResultError::OwnerSessionMismatch);
	assert(s.open(505,606,123) == H1ResultError::None);
	s.sequence = UINT64_MAX;
	assert(s.begin(H1ResultMode::Production,input,124) == H1ResultError::GenerationExhausted);
	assert(s.sequence == UINT64_MAX);
	s.epoch = UINT64_MAX;
	assert(s.open(707,808,125) == H1ResultError::GenerationExhausted);
	s.epochInverse = ~s.epoch;
	assert(!s.initialize() && s.epoch == UINT64_MAX);
	assert(!s.initialize() && s.epoch == UINT64_MAX && !s.bound);
	std::printf("PASS: ABI=%zu alignment=%zu wire=76 slot=1; selector, publication, failure, session, pin, source, timeout, exhaustion\n",
		sizeof(H1ProductionResult),alignof(H1ProductionResult));
}
